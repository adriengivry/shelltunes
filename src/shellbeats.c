#define _GNU_SOURCE

#include <locale.h>
#include <ncurses.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "config.h"
#include "download.h"
#include "input.h"
#include "log.h"
#include "playback.h"
#include "playlist.h"
#include "search.h"
#include "types.h"
#include "ui.h"
#include "updater.h"

int main(int argc, char *argv[]) {
    setlocale(LC_ALL, "");
    srand((unsigned int)time(NULL));

    // Open log file if -log flag is provided
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-log") == 0 || strcmp(argv[i], "--log") == 0) {
            const char *home = getenv("HOME");
            if (!home) home = "/tmp";
            char log_path[1024];
            snprintf(log_path, sizeof(log_path), "%s/.shellbeats/shellbeats.log", home);
            log_open(log_path);
            sb_log("========================================");
            sb_log("ShellBeats v0.6 started with -log");
            sb_log("HOME=%s", home);
            break;
        }
    }

    AppState st = {0};
    st.playing_index        = -1;
    st.playing_playlist_idx = -1;
    st.current_playlist_idx = -1;
    st.view                 = VIEW_SEARCH;

    pthread_mutex_init(&st.download_queue.mutex, NULL);
    st.download_queue.current_idx = -1;

    sb_log("Initializing config directories...");
    if (!init_config_dirs(&st)) {
        sb_log("FATAL: init_config_dirs failed");
        fprintf(stderr, "Failed to initialize config directory\n");
        return 1;
    }
    sb_log("Config dir: %s", st.config_dir);

    load_config(&st);
    load_playlists(&st);
    load_download_queue(&st);

    if (get_pending_download_count(&st) > 0) start_download_thread(&st);

    sb_log("Starting yt-dlp auto-update thread...");
    start_ytdlp_update(&st);

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    timeout(100);

    char status[512] = "";

    if (!check_dependencies(&st, status, sizeof(status))) {
        draw_ui(&st, status);
        timeout(-1);
        getch();
        endwin();
        fprintf(stderr, "%s\n", status);
        stop_download_thread(&st);
        stop_ytdlp_update(&st);
        pthread_mutex_destroy(&st.download_queue.mutex);
        log_close();
        return 1;
    }

    // Restore previous session if configured
    if (st.config.remember_session) {
        if (st.was_playing_playlist && st.last_playlist_idx >= 0 &&
            st.last_playlist_idx < st.playlist_count) {
            st.current_playlist_idx = st.last_playlist_idx;
            load_playlist_songs(&st, st.current_playlist_idx);
            if (st.last_song_idx >= 0 &&
                st.last_song_idx < st.playlists[st.current_playlist_idx].count)
                st.playlist_song_selected = st.last_song_idx;
            st.view = VIEW_PLAYLIST_SONGS;
            snprintf(status, sizeof(status), "Resuming: %s, track %d",
                     st.playlists[st.current_playlist_idx].name, st.last_song_idx + 1);
        } else if (st.cached_search_count > 0 && st.last_query[0]) {
            for (int i = 0; i < st.cached_search_count && i < MAX_RESULTS; i++) {
                st.search_results[i]        = st.cached_search[i];
                st.cached_search[i].title    = NULL;
                st.cached_search[i].video_id = NULL;
                st.cached_search[i].url      = NULL;
            }
            st.search_count = st.cached_search_count;
            snprintf(st.query, sizeof(st.query), "%s", st.last_query);
            if (st.last_song_idx >= 0 && st.last_song_idx < st.search_count)
                st.search_selected = st.last_song_idx;
            st.view = VIEW_SEARCH;
            snprintf(status, sizeof(status), "Resuming: search '%s', track %d",
                     st.query, st.last_song_idx + 1);
        } else {
            snprintf(status, sizeof(status),
                     "Press / to search, d to download, f for playlists, h for help.");
        }
    } else {
        snprintf(status, sizeof(status),
                 "Press / to search, d to download, f for playlists, h for help.");
    }

    draw_ui(&st, status);

    bool running = true;
    while (running) {
        // Update spinner
        time_t now = time(NULL);
        if (now != st.last_spinner_update) {
            st.spinner_frame++;
            st.last_spinner_update = now;
        }

        // Auto-advance on track end (after 3-second startup grace period)
        if (st.playing_index >= 0 && mpv_is_connected()) {
            if (now - st.playback_started >= 3) {
                if (mpv_check_track_end()) {
                    play_next(&st);
                    if (st.playing_index >= 0) {
                        const char *title = NULL;
                        if (st.playing_from_playlist && st.playing_playlist_idx >= 0) {
                            Playlist *pl = &st.playlists[st.playing_playlist_idx];
                            if (st.playing_index < pl->count)
                                title = pl->items[st.playing_index].title;
                        } else if (st.playing_index < st.search_count) {
                            title = st.search_results[st.playing_index].title;
                        }
                        if (title) snprintf(status, sizeof(status), "Auto-playing: %s", title);
                    } else {
                        snprintf(status, sizeof(status), "Playback finished");
                    }
                    draw_ui(&st, status);
                }
            }
        }

        int ch = getch();
        if (ch == ERR) {
            draw_ui(&st, status);
            continue;
        }

        bool quit = false;
        handle_input(&st, ch, status, sizeof(status), &quit);
        if (quit) break;

        draw_ui(&st, status);
    }

    // Save session on exit
    if (st.config.remember_session) {
        st.was_playing_playlist = st.playing_from_playlist;
        if (st.playing_from_playlist) {
            st.last_playlist_idx = st.playing_playlist_idx;
            st.last_song_idx     = st.playing_index;
        } else {
            st.last_playlist_idx = -1;
            st.last_song_idx     = st.search_selected;
            snprintf(st.last_query, sizeof(st.last_query), "%s", st.query);
            st.cached_search_count = st.search_count;
            for (int i = 0; i < st.search_count && i < MAX_RESULTS; i++) {
                free(st.cached_search[i].title);
                free(st.cached_search[i].video_id);
                free(st.cached_search[i].url);
                st.cached_search[i].title    = st.search_results[i].title
                                               ? strdup(st.search_results[i].title) : NULL;
                st.cached_search[i].video_id = st.search_results[i].video_id
                                               ? strdup(st.search_results[i].video_id) : NULL;
                st.cached_search[i].url      = st.search_results[i].url
                                               ? strdup(st.search_results[i].url) : NULL;
                st.cached_search[i].duration = st.search_results[i].duration;
            }
        }
        save_config(&st);
    }

    stop_download_thread(&st);
    stop_ytdlp_update(&st);
    pthread_mutex_destroy(&st.download_queue.mutex);

    endwin();

    free_search_results(&st);
    free_all_playlists(&st);
    mpv_quit();

    sb_log("ShellBeats exiting normally");
    log_close();

    return 0;
}
