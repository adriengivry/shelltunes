#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "playback.h"
#include "config.h"
#include "log.h"
#include "utils.h"

static pid_t mpv_pid    = -1;
static int   mpv_ipc_fd = -1;

// ============================================================================
// Internal IPC helpers
// ============================================================================

static void mpv_disconnect(void) {
    if (mpv_ipc_fd >= 0) {
        sb_log("[PLAYBACK] mpv_disconnect: closing fd=%d", mpv_ipc_fd);
        close(mpv_ipc_fd);
        mpv_ipc_fd = -1;
    }
}

static bool mpv_connect(void) {
    if (mpv_ipc_fd >= 0) return true;
    if (!file_exists(IPC_SOCKET)) return false;

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return false;

    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, IPC_SOCKET, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return false;
    }

    mpv_ipc_fd = fd;
    sb_log("[PLAYBACK] mpv_connect: connected (fd=%d)", fd);

    const char *observe = "{\"command\":[\"observe_property\",1,\"eof-reached\"]}\n";
    ssize_t w = write(mpv_ipc_fd, observe, strlen(observe));
    (void)w;
    return true;
}

static void mpv_send_command(const char *cmd) {
    sb_log("[PLAYBACK] mpv_send_command: %s", cmd);

    if (!mpv_connect()) {
        // One-shot fallback
        int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) return;

        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, IPC_SOCKET, sizeof(addr.sun_path) - 1);

        if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
            ssize_t w = write(fd, cmd, strlen(cmd));
            w = write(fd, "\n", 1);
            (void)w;
        }
        close(fd);
        return;
    }

    ssize_t w = write(mpv_ipc_fd, cmd, strlen(cmd));
    if (w < 0) sb_log("[PLAYBACK] mpv_send_command: write error: %s", strerror(errno));
    w = write(mpv_ipc_fd, "\n", 1);
    (void)w;
}

// ============================================================================
// Public API
// ============================================================================

bool mpv_is_connected(void) {
    return mpv_ipc_fd >= 0;
}

void mpv_start_if_needed(AppState *st) {
    sb_log("[PLAYBACK] mpv_start_if_needed");
    if (file_exists(IPC_SOCKET) && mpv_connect()) return;

    unlink(IPC_SOCKET);
    mpv_disconnect();

    const char *ytdlp_path = get_ytdlp_cmd(st);
    char ytdl_opt[1200];
    snprintf(ytdl_opt, sizeof(ytdl_opt),
             "--script-opts=ytdl_hook-ytdl_path=%s", ytdlp_path);

    pid_t pid = fork();
    if (pid == 0) {
        int fd = open("/dev/null", O_WRONLY);
        if (fd >= 0) { dup2(fd, STDOUT_FILENO); dup2(fd, STDERR_FILENO); close(fd); }
        execlp("mpv", "mpv",
               "--no-video", "--idle=yes", "--force-window=no", "--really-quiet",
               "--input-ipc-server=" IPC_SOCKET, ytdl_opt, (char *)NULL);
        _exit(127);
    }
    if (pid < 0) { sb_log("[PLAYBACK] fork() failed: %s", strerror(errno)); return; }

    mpv_pid = pid;
    sb_log("[PLAYBACK] mpv started (pid=%d)", pid);

    for (int i = 0; i < 100; i++) {
        if (file_exists(IPC_SOCKET)) {
            usleep(50 * 1000);
            if (mpv_connect()) { sb_log("[PLAYBACK] connected to mpv"); break; }
        }
        usleep(50 * 1000);
    }
}

void mpv_quit(void) {
    sb_log("[PLAYBACK] mpv_quit (pid=%d)", mpv_pid);
    mpv_send_command("{\"command\":[\"quit\"]}");
    usleep(100 * 1000);
    mpv_disconnect();
    if (mpv_pid > 0) {
        kill(mpv_pid, SIGTERM);
        waitpid(mpv_pid, NULL, WNOHANG);
        mpv_pid = -1;
    }
    unlink(IPC_SOCKET);
}

void mpv_toggle_pause(void) {
    sb_log("[PLAYBACK] mpv_toggle_pause");
    mpv_send_command("{\"command\":[\"cycle\",\"pause\"]}");
}

void mpv_stop_playback(void) {
    sb_log("[PLAYBACK] mpv_stop_playback");
    mpv_send_command("{\"command\":[\"stop\"]}");
}

void mpv_seek(int seconds) {
    sb_log("[PLAYBACK] mpv_seek: %+d s", seconds);
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "{\"command\":[\"seek\",\"%d\",\"relative\"]}", seconds);
    mpv_send_command(cmd);
}

void mpv_seek_absolute(int seconds) {
    sb_log("[PLAYBACK] mpv_seek_absolute: %d s", seconds);
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "{\"command\":[\"seek\",\"%d\",\"absolute\"]}", seconds);
    mpv_send_command(cmd);
}

void mpv_load_url(const char *url) {
    sb_log("[PLAYBACK] mpv_load_url: %s", url);

    char *escaped = NULL;
    size_t n = 0;
    FILE *mem = open_memstream(&escaped, &n);
    if (!mem) return;

    fputc('"', mem);
    for (const char *p = url; *p; p++) {
        if (*p == '"' || *p == '\\') fputc('\\', mem);
        fputc(*p, mem);
    }
    fputc('"', mem);
    fclose(mem);

    char cmd[4096];
    snprintf(cmd, sizeof(cmd), "{\"command\":[\"loadfile\",%s,\"replace\"]}", escaped);
    free(escaped);
    mpv_send_command(cmd);
}

bool mpv_check_track_end(void) {
    if (mpv_ipc_fd < 0) return false;

    char buf[4096];
    ssize_t n = read(mpv_ipc_fd, buf, sizeof(buf) - 1);

    if (n <= 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            sb_log("[PLAYBACK] mpv_check_track_end: connection lost");
            mpv_disconnect();
        }
        return false;
    }
    buf[n] = '\0';
    sb_log("[PLAYBACK] mpv IPC: %.200s", buf);

    if (strstr(buf, "\"event\":\"end-file\"") && strstr(buf, "\"reason\":\"eof\"")) {
        sb_log("[PLAYBACK] track ended (EOF)");
        return true;
    }
    return false;
}

// ============================================================================
// Playback control
// ============================================================================

void play_search_result(AppState *st, int idx) {
    if (idx < 0 || idx >= st->search_count || !st->search_results[idx].url) return;
    sb_log("[PLAYBACK] play_search_result #%d: %s", idx,
           st->search_results[idx].title ? st->search_results[idx].title : "(null)");

    mpv_start_if_needed(st);
    mpv_load_url(st->search_results[idx].url);

    st->playing_index        = idx;
    st->playing_from_playlist = false;
    st->playing_playlist_idx = -1;
    st->paused               = false;
    st->playback_started     = time(NULL);
}

void play_playlist_song(AppState *st, int playlist_idx, int song_idx) {
    if (playlist_idx < 0 || playlist_idx >= st->playlist_count) return;
    Playlist *pl = &st->playlists[playlist_idx];
    if (song_idx < 0 || song_idx >= pl->count || !pl->items[song_idx].url) return;

    sb_log("[PLAYBACK] play_playlist_song playlist=\"%s\" song=#%d \"%s\"",
           pl->name ? pl->name : "(null)", song_idx,
           pl->items[song_idx].title ? pl->items[song_idx].title : "(null)");

    mpv_start_if_needed(st);

    if (pl->is_youtube_playlist) {
        mpv_load_url(pl->items[song_idx].url);
    } else {
        char local_path[2048];
        if (get_local_file_path_for_song(st, pl->name, pl->items[song_idx].video_id,
                                          local_path, sizeof(local_path))) {
            sb_log("[PLAYBACK] playing local file: %s", local_path);
            mpv_load_url(local_path);
        } else {
            sb_log("[PLAYBACK] streaming: %s", pl->items[song_idx].url);
            mpv_load_url(pl->items[song_idx].url);
        }
    }

    st->playing_index        = song_idx;
    st->playing_from_playlist = true;
    st->playing_playlist_idx = playlist_idx;
    st->paused               = false;
    st->playback_started     = time(NULL);
}

static int get_random_index(int count, int current) {
    if (count <= 1) return 0;
    int next;
    do { next = rand() % count; } while (next == current && count > 1);
    return next;
}

void play_next(AppState *st) {
    sb_log("[PLAYBACK] play_next (idx=%d from_playlist=%d shuffle=%d)",
           st->playing_index, st->playing_from_playlist, st->config.shuffle_mode);

    if (st->playing_from_playlist && st->playing_playlist_idx >= 0) {
        Playlist *pl = &st->playlists[st->playing_playlist_idx];
        int next = st->config.shuffle_mode
            ? get_random_index(pl->count, st->playing_index)
            : st->playing_index + 1;

        if (next < pl->count || st->config.shuffle_mode) {
            if (next >= pl->count) next = get_random_index(pl->count, st->playing_index);
            play_playlist_song(st, st->playing_playlist_idx, next);
            st->playlist_song_selected = next;
        }
    } else if (st->search_count > 0) {
        int next = st->config.shuffle_mode
            ? get_random_index(st->search_count, st->playing_index)
            : st->playing_index + 1;

        if (next < st->search_count || st->config.shuffle_mode) {
            if (next >= st->search_count) next = get_random_index(st->search_count, st->playing_index);
            play_search_result(st, next);
            st->search_selected = next;
        }
    }
}

void play_prev(AppState *st) {
    sb_log("[PLAYBACK] play_prev (idx=%d)", st->playing_index);

    if (st->playing_from_playlist && st->playing_playlist_idx >= 0) {
        int prev = st->playing_index - 1;
        if (prev >= 0) {
            play_playlist_song(st, st->playing_playlist_idx, prev);
            st->playlist_song_selected = prev;
        }
    } else if (st->search_count > 0) {
        int prev = st->playing_index - 1;
        if (prev >= 0) {
            play_search_result(st, prev);
            st->search_selected = prev;
        }
    }
}
