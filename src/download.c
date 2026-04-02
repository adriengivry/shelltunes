#define _GNU_SOURCE

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "download.h"
#include "config.h"
#include "utils.h"

// ============================================================================
// Persistence
// ============================================================================

void save_download_queue(AppState *st) {
    FILE *f = fopen(st->download_queue_file, "w");
    if (!f) return;

    fprintf(f, "{\n  \"tasks\": [\n");
    bool first = true;

    for (int i = 0; i < st->download_queue.count; i++) {
        DownloadTask *t = &st->download_queue.tasks[i];
        if (t->status != DOWNLOAD_PENDING && t->status != DOWNLOAD_FAILED)
            continue;

        char *et = json_escape_string(t->title);
        char *ef = json_escape_string(t->sanitized_filename);
        char *ep = json_escape_string(t->playlist_name);

        if (!first) fprintf(f, ",\n");
        first = false;

        fprintf(f, "    {\"video_id\": \"%s\", \"title\": \"%s\", "
                   "\"filename\": \"%s\", \"playlist\": \"%s\", \"status\": \"%s\"}",
                t->video_id,
                et ? et : "", ef ? ef : "", ep ? ep : "",
                t->status == DOWNLOAD_FAILED ? "failed" : "pending");

        free(et); free(ef); free(ep);
    }
    fprintf(f, "\n  ]\n}\n");
    fclose(f);
}

void load_download_queue(AppState *st) {
    FILE *f = fopen(st->download_queue_file, "r");
    if (!f) return;

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fsize <= 0 || fsize > 1024 * 1024) { fclose(f); return; }

    char *content = malloc((size_t)fsize + 1);
    if (!content) { fclose(f); return; }

    size_t n = fread(content, 1, (size_t)fsize, f);
    content[n] = '\0';
    fclose(f);

    const char *p = strstr(content, "\"tasks\"");
    if (!p) { free(content); return; }
    p = strchr(p, '[');
    if (!p) { free(content); return; }

    pthread_mutex_lock(&st->download_queue.mutex);

    while (st->download_queue.count < MAX_DOWNLOAD_QUEUE) {
        const char *os = strchr(p, '{');
        if (!os) break;
        const char *oe = strchr(os, '}');
        if (!oe) break;

        size_t ol = (size_t)(oe - os) + 1;
        char *obj = malloc(ol + 1);
        if (!obj) break;
        memcpy(obj, os, ol);
        obj[ol] = '\0';

        char *video_id   = json_get_string(obj, "video_id");
        char *title      = json_get_string(obj, "title");
        char *filename   = json_get_string(obj, "filename");
        char *playlist   = json_get_string(obj, "playlist");
        char *status_str = json_get_string(obj, "status");
        free(obj);

        if (video_id && video_id[0]) {
            DownloadTask *t = &st->download_queue.tasks[st->download_queue.count];
            snprintf(t->video_id,           sizeof(t->video_id),           "%s", video_id);
            snprintf(t->title,              sizeof(t->title),              "%s", title    ? title    : "");
            snprintf(t->sanitized_filename, sizeof(t->sanitized_filename), "%s", filename ? filename : "");
            snprintf(t->playlist_name,      sizeof(t->playlist_name),      "%s", playlist ? playlist : "");

            if (status_str && strcmp(status_str, "failed") == 0) {
                t->status = DOWNLOAD_FAILED;
                st->download_queue.failed++;
            } else {
                t->status = DOWNLOAD_PENDING;
            }
            st->download_queue.count++;
        }

        free(video_id); free(title); free(filename); free(playlist); free(status_str);
        p = oe + 1;
    }

    pthread_mutex_unlock(&st->download_queue.mutex);
    free(content);
}

// ============================================================================
// Download thread
// ============================================================================

static void *download_thread_func(void *arg) {
    AppState *st = (AppState *)arg;

    while (!st->download_queue.should_stop) {
        pthread_mutex_lock(&st->download_queue.mutex);

        int task_idx = -1;
        for (int i = 0; i < st->download_queue.count; i++) {
            if (st->download_queue.tasks[i].status == DOWNLOAD_PENDING) {
                task_idx = i;
                st->download_queue.tasks[i].status = DOWNLOAD_ACTIVE;
                st->download_queue.current_idx     = i;
                st->download_queue.active          = true;
                break;
            }
        }

        if (task_idx < 0) {
            st->download_queue.active      = false;
            st->download_queue.current_idx = -1;
            pthread_mutex_unlock(&st->download_queue.mutex);
            usleep(500 * 1000);
            continue;
        }

        DownloadTask task;
        memcpy(&task, &st->download_queue.tasks[task_idx], sizeof(DownloadTask));
        pthread_mutex_unlock(&st->download_queue.mutex);

        char dest_dir[2048];
        if (task.playlist_name[0])
            snprintf(dest_dir, sizeof(dest_dir), "%s/%s", st->config.download_path, task.playlist_name);
        else
            snprintf(dest_dir, sizeof(dest_dir), "%s", st->config.download_path);

        mkdir_p(dest_dir);

        char dest_path[2560];
        snprintf(dest_path, sizeof(dest_path), "%s/%s", dest_dir, task.sanitized_filename);

        if (file_exists(dest_path)) {
            pthread_mutex_lock(&st->download_queue.mutex);
            st->download_queue.tasks[task_idx].status = DOWNLOAD_COMPLETED;
            st->download_queue.completed++;
            save_download_queue(st);
            pthread_mutex_unlock(&st->download_queue.mutex);
            continue;
        }

        char cmd[4096];
        snprintf(cmd, sizeof(cmd),
                 "%s -x --audio-format mp3 --no-playlist --quiet --no-warnings "
                 "-o '%s' 'https://www.youtube.com/watch?v=%s' >/dev/null 2>&1",
                 get_ytdlp_cmd(st), dest_path, task.video_id);

        int result = system(cmd);

        pthread_mutex_lock(&st->download_queue.mutex);
        if (result == 0 && file_exists(dest_path)) {
            st->download_queue.tasks[task_idx].status = DOWNLOAD_COMPLETED;
            st->download_queue.completed++;
        } else {
            st->download_queue.tasks[task_idx].status = DOWNLOAD_FAILED;
            st->download_queue.failed++;
        }
        save_download_queue(st);
        pthread_mutex_unlock(&st->download_queue.mutex);
    }

    return NULL;
}

void start_download_thread(AppState *st) {
    if (st->download_queue.thread_running) return;
    st->download_queue.should_stop = false;
    if (pthread_create(&st->download_queue.thread, NULL, download_thread_func, st) == 0)
        st->download_queue.thread_running = true;
}

void stop_download_thread(AppState *st) {
    if (!st->download_queue.thread_running) return;
    st->download_queue.should_stop = true;
    pthread_join(st->download_queue.thread, NULL);
    st->download_queue.thread_running = false;
}

// ============================================================================
// Queue management
// ============================================================================

int add_to_download_queue(AppState *st, const char *video_id,
                           const char *title, const char *playlist_name) {
    if (!video_id || !video_id[0]) return -1;

    char dest_dir[2048];
    if (playlist_name && playlist_name[0])
        snprintf(dest_dir, sizeof(dest_dir), "%s/%s", st->config.download_path, playlist_name);
    else
        snprintf(dest_dir, sizeof(dest_dir), "%s", st->config.download_path);

    if (file_exists_for_video(dest_dir, video_id)) return 0;

    pthread_mutex_lock(&st->download_queue.mutex);

    for (int i = 0; i < st->download_queue.count; i++) {
        if (strcmp(st->download_queue.tasks[i].video_id, video_id) == 0 &&
            st->download_queue.tasks[i].status == DOWNLOAD_PENDING) {
            pthread_mutex_unlock(&st->download_queue.mutex);
            return 0;
        }
    }

    if (st->download_queue.count >= MAX_DOWNLOAD_QUEUE) {
        pthread_mutex_unlock(&st->download_queue.mutex);
        return -1;
    }

    DownloadTask *t = &st->download_queue.tasks[st->download_queue.count];
    snprintf(t->video_id,      sizeof(t->video_id),      "%s", video_id);
    snprintf(t->title,         sizeof(t->title),         "%s", title ? title : "Unknown");
    snprintf(t->playlist_name, sizeof(t->playlist_name), "%s", playlist_name ? playlist_name : "");
    sanitize_title_for_filename(title, video_id, t->sanitized_filename, sizeof(t->sanitized_filename));
    t->status = DOWNLOAD_PENDING;
    st->download_queue.count++;

    save_download_queue(st);
    pthread_mutex_unlock(&st->download_queue.mutex);

    start_download_thread(st);
    return 1;
}

int get_pending_download_count(AppState *st) {
    pthread_mutex_lock(&st->download_queue.mutex);
    int count = 0;
    for (int i = 0; i < st->download_queue.count; i++) {
        DownloadStatus s = st->download_queue.tasks[i].status;
        if (s == DOWNLOAD_PENDING || s == DOWNLOAD_ACTIVE) count++;
    }
    pthread_mutex_unlock(&st->download_queue.mutex);
    return count;
}
