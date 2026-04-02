#define _GNU_SOURCE

#include <errno.h>
#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "input.h"
#include "config.h"
#include "download.h"
#include "playlist.h"
#include "playback.h"
#include "search.h"
#include "ui.h"
#include "utils.h"
#include "youtube_playlist.h"

// ============================================================================
// Settings editing (inline key handler — entered before view dispatch)
// ============================================================================

static bool handle_settings_edit(AppState *st, int ch, char *status, size_t status_sz) {
    switch (ch) {
        case 27: // Escape - cancel
            st->settings_editing = false;
            curs_set(0);
            snprintf(status, status_sz, "Edit cancelled");
            return true;

        case '\n':
        case KEY_ENTER:
            snprintf(st->config.download_path, sizeof(st->config.download_path),
                     "%s", st->settings_edit_buffer);
            save_config(st);
            st->settings_editing = false;
            curs_set(0);
            snprintf(status, status_sz, "Download path saved");
            return true;

        case KEY_BACKSPACE:
        case 127:
        case 8:
            if (st->settings_edit_pos > 0) {
                memmove(&st->settings_edit_buffer[st->settings_edit_pos - 1],
                        &st->settings_edit_buffer[st->settings_edit_pos],
                        strlen(&st->settings_edit_buffer[st->settings_edit_pos]) + 1);
                st->settings_edit_pos--;
            }
            return true;

        case KEY_DC:
            if (st->settings_edit_pos < (int)strlen(st->settings_edit_buffer)) {
                memmove(&st->settings_edit_buffer[st->settings_edit_pos],
                        &st->settings_edit_buffer[st->settings_edit_pos + 1],
                        strlen(&st->settings_edit_buffer[st->settings_edit_pos + 1]) + 1);
            }
            return true;

        case KEY_LEFT:
            if (st->settings_edit_pos > 0) st->settings_edit_pos--;
            return true;

        case KEY_RIGHT:
            if (st->settings_edit_pos < (int)strlen(st->settings_edit_buffer))
                st->settings_edit_pos++;
            return true;

        case KEY_HOME:
            st->settings_edit_pos = 0;
            return true;

        case KEY_END:
            st->settings_edit_pos = (int)strlen(st->settings_edit_buffer);
            return true;

        default:
            if (ch >= 32 && ch < 127) {
                int len = (int)strlen(st->settings_edit_buffer);
                if (len < (int)sizeof(st->settings_edit_buffer) - 1) {
                    memmove(&st->settings_edit_buffer[st->settings_edit_pos + 1],
                            &st->settings_edit_buffer[st->settings_edit_pos],
                            len - st->settings_edit_pos + 1);
                    st->settings_edit_buffer[st->settings_edit_pos] = ch;
                    st->settings_edit_pos++;
                }
            }
            return true;
    }
}

// ============================================================================
// Global keys (active in every view)
// ============================================================================

// Returns true if the key was consumed.
static bool handle_global_input(AppState *st, int ch,
                                 char *status, size_t status_sz,
                                 bool *quit) {
    switch (ch) {
        case 'q': {
            int pending = get_pending_download_count(st);
            if (pending > 0) {
                draw_exit_dialog_pub(st, pending);
                timeout(-1);
                int confirm = getch();
                timeout(100);
                if (confirm == 'q') *quit = true;
            } else {
                *quit = true;
            }
            return true;
        }

        case ' ':
            if (st->playing_index >= 0 && file_exists(IPC_SOCKET)) {
                mpv_toggle_pause();
                st->paused = !st->paused;
                snprintf(status, status_sz, st->paused ? "Paused" : "Playing");
            }
            return true;

        case 'n':
            if (st->playing_index >= 0) {
                play_next(st);
                snprintf(status, status_sz, "Next track");
            }
            return true;

        case 'p':
            if (st->playing_index >= 0) {
                play_prev(st);
                snprintf(status, status_sz, "Previous track");
            }
            return true;

        case 'h':
        case '?':
            show_help();
            return true;

        case 'R':
            st->config.shuffle_mode = !st->config.shuffle_mode;
            snprintf(status, status_sz, "Shuffle: %s",
                     st->config.shuffle_mode ? "ON" : "OFF");
            return true;

        case KEY_LEFT:
            if (st->view != VIEW_SETTINGS && st->playing_index >= 0 && file_exists(IPC_SOCKET)) {
                mpv_seek(-st->config.seek_step);
                snprintf(status, status_sz, "<< -%ds", st->config.seek_step);
            }
            return false; // let settings view also see KEY_LEFT if needed

        case KEY_RIGHT:
            if (st->view != VIEW_SETTINGS && st->playing_index >= 0 && file_exists(IPC_SOCKET)) {
                mpv_seek(st->config.seek_step);
                snprintf(status, status_sz, ">> +%ds", st->config.seek_step);
            }
            return false;

        case 't':
            if (st->playing_index >= 0 && file_exists(IPC_SOCKET)) {
                char time_input[16] = {0};
                get_string_input(time_input, sizeof(time_input), "Jump to (mm:ss): ");
                int mins = 0, secs = 0;
                if (sscanf(time_input, "%d:%d", &mins, &secs) == 2 ||
                    sscanf(time_input, "%d", &secs) == 1) {
                    mpv_seek_absolute(mins * 60 + secs);
                    snprintf(status, status_sz, "Jump to %d:%02d", mins, secs);
                } else {
                    snprintf(status, status_sz, "Invalid time format");
                }
            }
            return true;

        case 'i':
            st->view = VIEW_ABOUT;
            draw_ui(st, status);
            timeout(-1);
            getch();
            timeout(100);
            st->view = VIEW_SEARCH;
            return true;

        case 27: // Escape
            switch (st->view) {
                case VIEW_PLAYLISTS:
                    st->view = VIEW_SEARCH;
                    status[0] = '\0';
                    break;
                case VIEW_PLAYLIST_SONGS:
                    st->view = VIEW_PLAYLISTS;
                    status[0] = '\0';
                    break;
                case VIEW_ADD_TO_PLAYLIST:
                    st->view = VIEW_SEARCH;
                    st->song_to_add = NULL;
                    snprintf(status, status_sz, "Cancelled");
                    break;
                case VIEW_SETTINGS:
                    st->view = VIEW_SEARCH;
                    status[0] = '\0';
                    break;
                case VIEW_ABOUT:
                    st->view = VIEW_SEARCH;
                    status[0] = '\0';
                    break;
                default:
                    break;
            }
            return true;

        case KEY_RESIZE:
            clear();
            return true;

        default:
            return false;
    }
}

// ============================================================================
// Per-view input handlers
// ============================================================================

static void handle_search_input(AppState *st, int ch,
                                  char *status, size_t status_sz) {
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    (void)cols;
    int list_height = rows - 7;
    if (list_height < 1) list_height = 1;

    switch (ch) {
        case KEY_UP:
        case 'k':
            if (st->search_selected > 0) st->search_selected--;
            break;

        case KEY_DOWN:
        case 'j':
            if (st->search_selected + 1 < st->search_count) st->search_selected++;
            break;

        case KEY_PPAGE:
            st->search_selected -= list_height;
            if (st->search_selected < 0) st->search_selected = 0;
            break;

        case KEY_NPAGE:
            st->search_selected += list_height;
            if (st->search_selected >= st->search_count)
                st->search_selected = st->search_count - 1;
            if (st->search_selected < 0) st->search_selected = 0;
            break;

        case KEY_HOME:
        case 'g':
            st->search_selected = 0;
            st->search_scroll   = 0;
            break;

        case KEY_END:
            if (st->search_count > 0) st->search_selected = st->search_count - 1;
            break;

        case '\n':
        case KEY_ENTER:
            if (st->search_count > 0) {
                play_search_result(st, st->search_selected);
                snprintf(status, status_sz, "Playing: %s",
                         st->search_results[st->search_selected].title ?
                         st->search_results[st->search_selected].title : "?");
            }
            break;

        case '/':
        case 's': {
            char q[256] = {0};
            if (get_string_input(q, sizeof(q), "Search: ") > 0) {
                snprintf(status, status_sz, "Searching: %s ...", q);
                draw_ui(st, status);
                int r = run_search(st, q);
                if      (r < 0) snprintf(status, status_sz, "Search error!");
                else if (r == 0) snprintf(status, status_sz, "No results for: %s", q);
                else             snprintf(status, status_sz, "Found %d results for: %s", r, q);
            } else {
                snprintf(status, status_sz, "Search cancelled");
            }
            break;
        }

        case 'x':
            if (st->playing_index >= 0) {
                mpv_stop_playback();
                st->playing_index        = -1;
                st->playing_from_playlist = false;
                st->playing_playlist_idx  = -1;
                st->paused               = false;
                snprintf(status, status_sz, "Playback stopped");
            }
            break;

        case 'f':
            st->view             = VIEW_PLAYLISTS;
            st->playlist_selected = 0;
            st->playlist_scroll   = 0;
            load_playlists(st);
            snprintf(status, status_sz, "Playlists");
            break;

        case 'a':
            if (st->search_count > 0) {
                st->song_to_add              = &st->search_results[st->search_selected];
                st->add_to_playlist_selected = 0;
                st->add_to_playlist_scroll   = 0;
                st->view                     = VIEW_ADD_TO_PLAYLIST;
                snprintf(status, status_sz, "Select playlist");
            } else {
                snprintf(status, status_sz, "No song selected");
            }
            break;

        case 'c': {
            char name[128] = {0};
            int  len       = get_string_input(name, sizeof(name), "New playlist name: ");
            if (len > 0) {
                int idx = create_playlist(st, name, false);
                if      (idx >= 0)  snprintf(status, status_sz, "Created playlist: %s", name);
                else if (idx == -2) snprintf(status, status_sz, "Playlist already exists: %s", name);
                else                snprintf(status, status_sz, "Failed to create playlist");
            } else {
                snprintf(status, status_sz, "Cancelled");
            }
            break;
        }

        case 'S':
            st->view             = VIEW_SETTINGS;
            st->settings_selected = 0;
            st->settings_editing  = false;
            snprintf(status, status_sz, "Settings");
            break;

        case 'd':
            if (st->search_count > 0) {
                Song *song   = &st->search_results[st->search_selected];
                int   result = add_to_download_queue(st, song->video_id, song->title, NULL);
                if      (result > 0) snprintf(status, status_sz, "Queued: %s", song->title);
                else if (result == 0) snprintf(status, status_sz, "Already downloaded or queued");
                else                  snprintf(status, status_sz, "Failed to queue download");
            } else {
                snprintf(status, status_sz, "No song selected");
            }
            break;
    }
}

static void handle_playlists_input(AppState *st, int ch,
                                    char *status, size_t status_sz) {
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    (void)cols;
    int list_height = rows - 7;
    if (list_height < 1) list_height = 1;

    switch (ch) {
        case KEY_UP:
        case 'k':
            if (st->playlist_selected > 0) st->playlist_selected--;
            break;

        case KEY_DOWN:
        case 'j':
            if (st->playlist_selected + 1 < st->playlist_count) st->playlist_selected++;
            break;

        case KEY_PPAGE:
            st->playlist_selected -= list_height;
            if (st->playlist_selected < 0) st->playlist_selected = 0;
            break;

        case KEY_NPAGE:
            st->playlist_selected += list_height;
            if (st->playlist_selected >= st->playlist_count)
                st->playlist_selected = st->playlist_count - 1;
            if (st->playlist_selected < 0) st->playlist_selected = 0;
            break;

        case '\n':
        case KEY_ENTER:
            if (st->playlist_count > 0) {
                st->current_playlist_idx   = st->playlist_selected;
                load_playlist_songs(st, st->current_playlist_idx);
                st->playlist_song_selected = 0;
                st->playlist_song_scroll   = 0;
                st->view                   = VIEW_PLAYLIST_SONGS;
                snprintf(status, status_sz, "Opened: %s",
                         st->playlists[st->current_playlist_idx].name);
            }
            break;

        case 'c': {
            char name[128] = {0};
            int  len       = get_string_input(name, sizeof(name), "New playlist name: ");
            if (len > 0) {
                int idx = create_playlist(st, name, false);
                if (idx >= 0) {
                    snprintf(status, status_sz, "Created playlist: %s", name);
                    st->playlist_selected = idx;
                } else if (idx == -2) {
                    snprintf(status, status_sz, "Playlist already exists: %s", name);
                } else {
                    snprintf(status, status_sz, "Failed to create playlist");
                }
            } else {
                snprintf(status, status_sz, "Cancelled");
            }
            break;
        }

        case 'x':
            if (st->playlist_count > 0) {
                char confirm[8] = {0};
                char prompt[256];
                snprintf(prompt, sizeof(prompt), "Delete '%s'? (y/n): ",
                         st->playlists[st->playlist_selected].name);
                get_string_input(confirm, sizeof(confirm), prompt);
                if (confirm[0] == 'y' || confirm[0] == 'Y') {
                    if (delete_playlist(st, st->playlist_selected)) {
                        snprintf(status, status_sz, "Deleted playlist");
                        if (st->playlist_selected >= st->playlist_count && st->playlist_count > 0)
                            st->playlist_selected = st->playlist_count - 1;
                    } else {
                        snprintf(status, status_sz, "Failed to delete");
                    }
                } else {
                    snprintf(status, status_sz, "Cancelled");
                }
            }
            break;

        case 'e': {
            if (st->playlist_count == 0) break;
            Playlist *pl = &st->playlists[st->playlist_selected];
            char new_name[256] = {0};
            char prompt[384];
            snprintf(prompt, sizeof(prompt), "Rename '%s' to: ", pl->name);
            int len = get_string_input(new_name, sizeof(new_name), prompt);
            if (len == 0) { snprintf(status, status_sz, "Cancelled"); break; }

            bool exists = false;
            for (int i = 0; i < st->playlist_count; i++) {
                if (i != st->playlist_selected &&
                    strcmp(st->playlists[i].name, new_name) == 0) {
                    exists = true; break;
                }
            }
            if (exists) {
                snprintf(status, status_sz, "Playlist '%s' already exists", new_name);
                break;
            }

            // Build new filename using sanitize_filename() from utils
            char *new_filename = sanitize_filename(new_name);
            if (!new_filename) { snprintf(status, status_sz, "Failed to rename"); break; }

            char old_json_path[4096], new_json_path[4096];
            char old_dl_path[4096],  new_dl_path[4096];

            snprintf(old_json_path, sizeof(old_json_path), "%s/%s", st->playlists_dir, pl->filename);
            snprintf(new_json_path, sizeof(new_json_path), "%s/%s", st->playlists_dir, new_filename);
            snprintf(old_dl_path,   sizeof(old_dl_path),  "%s/%s", st->config.download_path, pl->name);
            snprintf(new_dl_path,   sizeof(new_dl_path),  "%s/%s", st->config.download_path, new_name);

            bool ok = true;
            if (rename(old_json_path, new_json_path) != 0 && errno != ENOENT) ok = false;
            if (ok && dir_exists(old_dl_path) && rename(old_dl_path, new_dl_path) != 0) {
                rename(new_json_path, old_json_path);
                ok = false;
            }

            if (ok) {
                free(pl->name);     pl->name     = strdup(new_name);
                free(pl->filename); pl->filename = new_filename;
                save_playlists_index(st);
                save_playlist(st, st->playlist_selected);
                snprintf(status, status_sz, "Renamed to '%s'", new_name);
            } else {
                free(new_filename);
                snprintf(status, status_sz, "Failed to rename playlist");
            }
            break;
        }

        case 'p': {
            char url[512] = {0};
            int  len      = get_string_input(url, sizeof(url), "YouTube playlist URL: ");
            if (len == 0) { snprintf(status, status_sz, "Cancelled"); break; }
            if (!validate_youtube_playlist_url(url)) {
                snprintf(status, status_sz, "Invalid URL");
                break;
            }

            snprintf(status, status_sz, "Validating URL...");
            draw_ui(st, status);

            char fetched_title[256] = {0};
            Song temp_songs[MAX_PLAYLIST_ITEMS];
            YTProgressCtx ctx = { st, status, status_sz };
            int fetched = fetch_youtube_playlist(url, temp_songs, MAX_PLAYLIST_ITEMS,
                                                  fetched_title, sizeof(fetched_title),
                                                  yt_progress_cb, &ctx, get_ytdlp_cmd(st));
            if (fetched <= 0) {
                snprintf(status, status_sz, "Failed to fetch playlist");
                break;
            }

            char playlist_name[256];
            int name_len = get_string_input(playlist_name, sizeof(playlist_name), "Playlist name: ");
            if (name_len == 0) {
                snprintf(playlist_name, sizeof(playlist_name), "%s", fetched_title);
            }

            char mode[8] = {0};
            while (1) {
                get_string_input(mode, sizeof(mode), "Mode (s)tream or (d)ownload: ");
                if (mode[0] == 's' || mode[0] == 'S' || mode[0] == 'd' || mode[0] == 'D') break;
                snprintf(status, status_sz, "Invalid mode. Choose 's' or 'd'");
                draw_ui(st, status);
            }
            bool stream_only = (mode[0] == 's' || mode[0] == 'S');

            int idx = create_playlist(st, playlist_name, true);
            if (idx < 0) {
                snprintf(status, status_sz, "Failed to create playlist");
                for (int i = 0; i < fetched; i++) {
                    free(temp_songs[i].title);
                    free(temp_songs[i].video_id);
                    free(temp_songs[i].url);
                }
                break;
            }

            Playlist *pl = &st->playlists[idx];
            for (int i = 0; i < fetched; i++) {
                pl->items[i] = temp_songs[i];
                pl->count++;
            }
            save_playlist(st, idx);

            if (!stream_only) {
                for (int i = 0; i < pl->count; i++)
                    add_to_download_queue(st, pl->items[i].video_id, pl->items[i].title, pl->name);
            }
            status[0] = '\0';
            break;
        }

        case 'd':
            if (st->playlist_count > 0) {
                Playlist *pl = &st->playlists[st->playlist_selected];
                if (pl->count == 0) load_playlist_songs(st, st->playlist_selected);

                int added = 0, skipped = 0;
                for (int i = 0; i < pl->count; i++) {
                    int result = add_to_download_queue(st, pl->items[i].video_id,
                                                        pl->items[i].title, pl->name);
                    if      (result > 0) added++;
                    else if (result == 0) skipped++;
                }

                if      (added   > 0) snprintf(status, status_sz, "Queued %d songs (%d already downloaded)", added, skipped);
                else if (skipped > 0) snprintf(status, status_sz, "All %d songs already downloaded", skipped);
                else                   snprintf(status, status_sz, "Playlist is empty");
            }
            break;
    }
}

static void handle_playlist_songs_input(AppState *st, int ch,
                                          char *status, size_t status_sz) {
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    (void)cols;
    int list_height = rows - 7;
    if (list_height < 1) list_height = 1;

    Playlist *pl = NULL;
    if (st->current_playlist_idx >= 0 && st->current_playlist_idx < st->playlist_count)
        pl = &st->playlists[st->current_playlist_idx];

    switch (ch) {
        case KEY_UP:
        case 'k':
            if (st->playlist_song_selected > 0) st->playlist_song_selected--;
            break;

        case KEY_DOWN:
        case 'j':
            if (pl && st->playlist_song_selected + 1 < pl->count) st->playlist_song_selected++;
            break;

        case KEY_PPAGE:
            st->playlist_song_selected -= list_height;
            if (st->playlist_song_selected < 0) st->playlist_song_selected = 0;
            break;

        case KEY_NPAGE:
            if (pl) {
                st->playlist_song_selected += list_height;
                if (st->playlist_song_selected >= pl->count)
                    st->playlist_song_selected = pl->count - 1;
                if (st->playlist_song_selected < 0) st->playlist_song_selected = 0;
            }
            break;

        case '\n':
        case KEY_ENTER:
            if (pl && pl->count > 0) {
                play_playlist_song(st, st->current_playlist_idx, st->playlist_song_selected);
                snprintf(status, status_sz, "Playing: %s",
                         pl->items[st->playlist_song_selected].title ?
                         pl->items[st->playlist_song_selected].title : "?");
            }
            break;

        case 'd':
            if (pl && pl->count > 0) {
                Song *song   = &pl->items[st->playlist_song_selected];
                int   result = add_to_download_queue(st, song->video_id, song->title, pl->name);
                if      (result > 0) snprintf(status, status_sz, "Queued: %s", song->title);
                else if (result == 0) snprintf(status, status_sz, "Already downloaded or queued");
                else                  snprintf(status, status_sz, "Failed to queue download");
            } else {
                snprintf(status, status_sz, "No song selected");
            }
            break;

        case 'r':
            if (pl && pl->count > 0) {
                const char *title = pl->items[st->playlist_song_selected].title;
                if (remove_song_from_playlist(st, st->current_playlist_idx,
                                               st->playlist_song_selected)) {
                    snprintf(status, status_sz, "Removed: %s", title ? title : "?");
                    if (st->playlist_song_selected >= pl->count && pl->count > 0)
                        st->playlist_song_selected = pl->count - 1;
                } else {
                    snprintf(status, status_sz, "Failed to remove");
                }
            }
            break;

        case 'D':
            if (pl && pl->count > 0) {
                int added = 0;
                for (int i = 0; i < pl->count; i++) {
                    int result = add_to_download_queue(st, pl->items[i].video_id,
                                                        pl->items[i].title, pl->name);
                    if (result > 0) added++;
                }
                if (added > 0) snprintf(status, status_sz, "Queued %d songs", added);
                else           snprintf(status, status_sz, "All songs already queued or downloaded");
            }
            break;

        case 'u':
            if (pl && pl->is_youtube_playlist) {
                snprintf(status, status_sz, "Syncing playlist...");
                draw_ui(st, status);

                char fetch_url[512] = {0};
                int  len = get_string_input(fetch_url, sizeof(fetch_url),
                               "YouTube playlist URL to sync: ");

                if (len > 0 && validate_youtube_playlist_url(fetch_url)) {
                    char fetched_title[256] = {0};
                    Song temp_songs[MAX_PLAYLIST_ITEMS];
                    YTProgressCtx ctx = { st, status, status_sz };
                    int fetched = fetch_youtube_playlist(fetch_url, temp_songs, MAX_PLAYLIST_ITEMS,
                                                          fetched_title, sizeof(fetched_title),
                                                          yt_progress_cb, &ctx, get_ytdlp_cmd(st));

                    if (fetched > 0) {
                        int new_count = 0;
                        int old_count = pl->count;

                        for (int i = 0; i < fetched; i++) {
                            bool exists = false;
                            for (int j = 0; j < old_count; j++) {
                                if (pl->items[j].video_id && temp_songs[i].video_id &&
                                    strcmp(pl->items[j].video_id, temp_songs[i].video_id) == 0) {
                                    exists = true; break;
                                }
                            }

                            if (!exists && pl->count < MAX_PLAYLIST_ITEMS) {
                                int idx            = pl->count;
                                pl->items[idx]     = temp_songs[i];
                                pl->count++;
                                new_count++;
                                temp_songs[i].title    = NULL;
                                temp_songs[i].video_id = NULL;
                                temp_songs[i].url      = NULL;
                            }
                        }

                        for (int i = 0; i < fetched; i++) {
                            free(temp_songs[i].title);
                            free(temp_songs[i].video_id);
                            free(temp_songs[i].url);
                        }

                        if (new_count > 0) {
                            save_playlist(st, st->current_playlist_idx);
                            snprintf(status, status_sz, "Added %d new songs", new_count);
                        } else {
                            snprintf(status, status_sz, "Playlist is up to date");
                        }
                    } else {
                        snprintf(status, status_sz, "Failed to fetch playlist");
                    }
                } else if (len > 0) {
                    snprintf(status, status_sz, "Invalid YouTube playlist URL");
                } else {
                    snprintf(status, status_sz, "Sync cancelled");
                }
            } else {
                snprintf(status, status_sz, "Not a YouTube playlist");
            }
            break;

        case 'x':
            if (st->playing_index >= 0) {
                mpv_stop_playback();
                st->playing_index        = -1;
                st->playing_from_playlist = false;
                st->playing_playlist_idx  = -1;
                st->paused               = false;
                snprintf(status, status_sz, "Playback stopped");
            }
            break;
    }
}

static void handle_add_to_playlist_input(AppState *st, int ch,
                                           char *status, size_t status_sz) {
    switch (ch) {
        case KEY_UP:
        case 'k':
            if (st->add_to_playlist_selected > 0) st->add_to_playlist_selected--;
            break;

        case KEY_DOWN:
        case 'j':
            if (st->add_to_playlist_selected + 1 < st->playlist_count)
                st->add_to_playlist_selected++;
            break;

        case '\n':
        case KEY_ENTER:
            if (st->playlist_count > 0 && st->song_to_add) {
                if (add_song_to_playlist(st, st->add_to_playlist_selected, st->song_to_add))
                    snprintf(status, status_sz, "Added to: %s",
                             st->playlists[st->add_to_playlist_selected].name);
                else
                    snprintf(status, status_sz, "Already in playlist or failed");
                st->song_to_add = NULL;
                st->view        = VIEW_SEARCH;
            }
            break;

        case 'c': {
            char name[128] = {0};
            int  len       = get_string_input(name, sizeof(name), "New playlist name: ");
            if (len > 0) {
                int idx = create_playlist(st, name, false);
                if (idx >= 0) {
                    if (st->song_to_add) {
                        add_song_to_playlist(st, idx, st->song_to_add);
                        snprintf(status, status_sz, "Created '%s' and added song", name);
                        st->song_to_add = NULL;
                        st->view        = VIEW_SEARCH;
                    } else {
                        snprintf(status, status_sz, "Created: %s", name);
                    }
                } else if (idx == -2) {
                    snprintf(status, status_sz, "Playlist already exists: %s", name);
                } else {
                    snprintf(status, status_sz, "Failed to create playlist");
                }
            } else {
                snprintf(status, status_sz, "Cancelled");
            }
            break;
        }
    }
}

static void handle_settings_input(AppState *st, int ch,
                                    char *status, size_t status_sz) {
    switch (ch) {
        case KEY_UP:
        case 'k':
            if (st->settings_selected > 0) st->settings_selected--;
            break;

        case KEY_DOWN:
        case 'j':
            if (st->settings_selected < 3) st->settings_selected++;
            break;

        case '\n':
        case KEY_ENTER:
            if (st->settings_selected == 0) {
                st->settings_editing = true;
                snprintf(st->settings_edit_buffer, sizeof(st->settings_edit_buffer),
                         "%s", st->config.download_path);
                st->settings_edit_pos = (int)strlen(st->settings_edit_buffer);
                snprintf(status, status_sz, "Editing download path...");
            } else if (st->settings_selected == 1) {
                char step_input[16] = {0};
                int  len = get_string_input(step_input, sizeof(step_input),
                               "Seek step (1-300 seconds): ");
                if (len > 0) {
                    int new_step = atoi(step_input);
                    if (new_step >= 1 && new_step <= 300) {
                        st->config.seek_step = new_step;
                        save_config(st);
                        snprintf(status, status_sz, "Seek step set to %d seconds", new_step);
                    } else {
                        snprintf(status, status_sz, "Invalid value (must be 1-300)");
                    }
                }
            } else if (st->settings_selected == 2) {
                st->config.remember_session = !st->config.remember_session;
                save_config(st);
                snprintf(status, status_sz, "Remember session: %s",
                         st->config.remember_session ? "ON" : "OFF");
            } else if (st->settings_selected == 3) {
                st->config.shuffle_mode = !st->config.shuffle_mode;
                snprintf(status, status_sz, "Shuffle: %s",
                         st->config.shuffle_mode ? "ON" : "OFF");
            }
            break;
    }
}

// ============================================================================
// Public API
// ============================================================================

void handle_input(AppState *st, int ch, char *status, size_t status_sz, bool *quit) {
    // Settings edit mode intercepts all keys first
    if (st->view == VIEW_SETTINGS && st->settings_editing) {
        handle_settings_edit(st, ch, status, status_sz);
        return;
    }

    // Global keys (handled in every view; may return early)
    if (handle_global_input(st, ch, status, status_sz, quit)) return;

    // View-specific handling
    switch (st->view) {
        case VIEW_SEARCH:
            handle_search_input(st, ch, status, status_sz);
            break;
        case VIEW_PLAYLISTS:
            handle_playlists_input(st, ch, status, status_sz);
            break;
        case VIEW_PLAYLIST_SONGS:
            handle_playlist_songs_input(st, ch, status, status_sz);
            break;
        case VIEW_ADD_TO_PLAYLIST:
            handle_add_to_playlist_input(st, ch, status, status_sz);
            break;
        case VIEW_SETTINGS:
            handle_settings_input(st, ch, status, status_sz);
            break;
        case VIEW_ABOUT:
            break;
    }
}
