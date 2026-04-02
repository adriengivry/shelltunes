#define _GNU_SOURCE

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "updater.h"
#include "log.h"
#include "utils.h"

static void *ytdlp_update_thread_func(void *arg) {
    AppState *st = (AppState *)arg;

    sb_log("yt-dlp update thread started (local=%s)", st->ytdlp_local_path);
    snprintf(st->ytdlp_update_status, sizeof(st->ytdlp_update_status),
             "Checking for yt-dlp updates...");

    bool has_curl = (system("command -v curl >/dev/null 2>&1") == 0);
    bool has_wget = (system("command -v wget >/dev/null 2>&1") == 0);

    if (!has_curl && !has_wget) {
        sb_log("yt-dlp update: no curl or wget found");
        snprintf(st->ytdlp_update_status, sizeof(st->ytdlp_update_status),
                 "No curl or wget found");
        st->ytdlp_updating     = false;
        st->ytdlp_update_done  = true;
        return NULL;
    }

    // Fetch latest version tag via GitHub redirect
    char version_cmd[512];
    if (has_curl) {
        snprintf(version_cmd, sizeof(version_cmd),
                 "curl -sL -o /dev/null -w '%%{url_effective}' "
                 "'https://github.com/yt-dlp/yt-dlp/releases/latest' 2>/dev/null");
    } else {
        snprintf(version_cmd, sizeof(version_cmd),
                 "wget --spider -S --max-redirect=5 "
                 "'https://github.com/yt-dlp/yt-dlp/releases/latest' 2>&1 "
                 "| grep -i 'Location:' | tail -1 | awk '{print $2}'");
    }

    FILE *fp = popen(version_cmd, "r");
    if (!fp) {
        snprintf(st->ytdlp_update_status, sizeof(st->ytdlp_update_status), "Update check failed");
        st->ytdlp_updating = false; st->ytdlp_update_done = true;
        return NULL;
    }

    char redirect_url[512] = {0};
    if (!fgets(redirect_url, sizeof(redirect_url), fp)) redirect_url[0] = '\0';
    pclose(fp);

    // Extract version tag from URL (e.g. .../tag/2025.01.26)
    char *tag = strrchr(redirect_url, '/');
    if (!tag || strlen(tag) < 2) {
        snprintf(st->ytdlp_update_status, sizeof(st->ytdlp_update_status),
                 "No network or failed to check version");
        st->ytdlp_updating = false; st->ytdlp_update_done = true;
        return NULL;
    }
    tag++;

    size_t tag_len = strlen(tag);
    while (tag_len > 0 && (tag[tag_len - 1] == '\n' || tag[tag_len - 1] == '\r' ||
                            tag[tag_len - 1] == ' '))
        tag[--tag_len] = '\0';

    if (tag_len == 0) {
        snprintf(st->ytdlp_update_status, sizeof(st->ytdlp_update_status),
                 "Could not parse yt-dlp version");
        st->ytdlp_updating = false; st->ytdlp_update_done = true;
        return NULL;
    }

    sb_log("yt-dlp update: remote tag = '%s'", tag);

    // Check if local version matches
    bool needs_download = true;
    if (file_exists(st->ytdlp_version_file) && file_exists(st->ytdlp_local_path)) {
        FILE *vf = fopen(st->ytdlp_version_file, "r");
        if (vf) {
            char local_ver[128] = {0};
            if (fgets(local_ver, sizeof(local_ver), vf)) {
                size_t lv_len = strlen(local_ver);
                while (lv_len > 0 && (local_ver[lv_len - 1] == '\n' || local_ver[lv_len - 1] == '\r'))
                    local_ver[--lv_len] = '\0';
                if (strcmp(local_ver, tag) == 0) needs_download = false;
            }
            fclose(vf);
        }
    }

    if (!needs_download) {
        sb_log("yt-dlp is already up to date (%s)", tag);
        snprintf(st->ytdlp_update_status, sizeof(st->ytdlp_update_status),
                 "yt-dlp is up to date (%s)", tag);
        st->ytdlp_has_local    = true;
        st->ytdlp_updating     = false;
        st->ytdlp_update_done  = true;
        return NULL;
    }

    snprintf(st->ytdlp_update_status, sizeof(st->ytdlp_update_status),
             "Downloading yt-dlp %s...", tag);

    char dl_cmd[5120];
    if (has_curl) {
        snprintf(dl_cmd, sizeof(dl_cmd),
                 "curl -sL 'https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp' "
                 "-o '%s' 2>/dev/null && chmod +x '%s'",
                 st->ytdlp_local_path, st->ytdlp_local_path);
    } else {
        snprintf(dl_cmd, sizeof(dl_cmd),
                 "wget -q 'https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp' "
                 "-O '%s' 2>/dev/null && chmod +x '%s'",
                 st->ytdlp_local_path, st->ytdlp_local_path);
    }

    int result = system(dl_cmd);
    if (result == 0 && file_exists(st->ytdlp_local_path)) {
        FILE *vf = fopen(st->ytdlp_version_file, "w");
        if (vf) { fprintf(vf, "%s\n", tag); fclose(vf); }
        st->ytdlp_has_local = true;
        snprintf(st->ytdlp_update_status, sizeof(st->ytdlp_update_status),
                 "yt-dlp updated to %s", tag);
        sb_log("yt-dlp updated to %s", tag);
    } else {
        snprintf(st->ytdlp_update_status, sizeof(st->ytdlp_update_status),
                 "yt-dlp download failed");
        sb_log("yt-dlp download failed (result=%d)", result);
    }

    st->ytdlp_updating    = false;
    st->ytdlp_update_done = true;
    return NULL;
}

void start_ytdlp_update(AppState *st) {
    if (st->ytdlp_update_thread_running) return;
    st->ytdlp_has_local    = file_exists(st->ytdlp_local_path);
    st->ytdlp_updating     = true;
    st->ytdlp_update_done  = false;
    if (pthread_create(&st->ytdlp_update_thread, NULL, ytdlp_update_thread_func, st) == 0)
        st->ytdlp_update_thread_running = true;
    else
        st->ytdlp_updating = false;
}

void stop_ytdlp_update(AppState *st) {
    if (!st->ytdlp_update_thread_running) return;
    pthread_join(st->ytdlp_update_thread, NULL);
    st->ytdlp_update_thread_running = false;
}
