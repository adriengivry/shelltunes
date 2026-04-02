#define _GNU_SOURCE

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "config.h"
#include "log.h"
#include "utils.h"

// ============================================================================
// Path initialisation
// ============================================================================

bool init_config_dirs(AppState *st) {
    const char *home = getenv("HOME");
    if (!home) home = "/tmp";

    snprintf(st->config_dir,          sizeof(st->config_dir),          "%s/%s",    home,          CONFIG_DIR);
    snprintf(st->playlists_dir,        sizeof(st->playlists_dir),        "%s/%s",    st->config_dir, PLAYLISTS_DIR);
    snprintf(st->playlists_index,      sizeof(st->playlists_index),      "%s/%s",    st->config_dir, PLAYLISTS_INDEX);
    snprintf(st->config_file,          sizeof(st->config_file),          "%s/%s",    st->config_dir, CONFIG_FILE);
    snprintf(st->download_queue_file,  sizeof(st->download_queue_file),  "%s/%s",    st->config_dir, DOWNLOAD_QUEUE_FILE);
    snprintf(st->ytdlp_bin_dir,        sizeof(st->ytdlp_bin_dir),        "%s/%s",    st->config_dir, YTDLP_BIN_DIR);
    snprintf(st->ytdlp_local_path,     sizeof(st->ytdlp_local_path),     "%s/%s/%s", st->config_dir, YTDLP_BIN_DIR, YTDLP_BINARY);
    snprintf(st->ytdlp_version_file,   sizeof(st->ytdlp_version_file),   "%s/%s",    st->config_dir, YTDLP_VERSION_FILE);

    if (!dir_exists(st->config_dir) && mkdir(st->config_dir, 0755) != 0)
        return false;
    if (!dir_exists(st->playlists_dir) && mkdir(st->playlists_dir, 0755) != 0)
        return false;

    // yt-dlp bin dir is optional (auto-update)
    if (!dir_exists(st->ytdlp_bin_dir))
        mkdir(st->ytdlp_bin_dir, 0755);

    if (!file_exists(st->playlists_index)) {
        FILE *f = fopen(st->playlists_index, "w");
        if (f) { fprintf(f, "{\"playlists\":[]}\n"); fclose(f); }
    }

    return true;
}

// ============================================================================
// Default config
// ============================================================================

void init_default_config(AppState *st) {
    const char *home = getenv("HOME");
    if (!home) home = "/tmp";

    snprintf(st->config.download_path, sizeof(st->config.download_path),
             "%s/Music/shellbeats", home);
    st->config.seek_step      = 10;
    st->config.remember_session = false;
    st->config.shuffle_mode   = false;
}

// ============================================================================
// Persistence
// ============================================================================

void save_config(AppState *st) {
    FILE *f = fopen(st->config_file, "w");
    if (!f) return;

    char *esc_path  = json_escape_string(st->config.download_path);
    char *esc_query = json_escape_string(st->last_query);

    fprintf(f, "{\n");
    fprintf(f, "  \"download_path\": \"%s\",\n",    esc_path  ? esc_path  : "");
    fprintf(f, "  \"seek_step\": %d,\n",             st->config.seek_step);
    fprintf(f, "  \"remember_session\": %s,\n",      st->config.remember_session ? "true" : "false");
    fprintf(f, "  \"shuffle_mode\": %s,\n",          st->config.shuffle_mode     ? "true" : "false");

    if (st->config.remember_session) {
        fprintf(f, "  \"last_query\": \"%s\",\n",         esc_query ? esc_query : "");
        fprintf(f, "  \"last_playlist_idx\": %d,\n",      st->last_playlist_idx);
        fprintf(f, "  \"last_song_idx\": %d,\n",          st->last_song_idx);
        fprintf(f, "  \"was_playing_playlist\": %s,\n",   st->was_playing_playlist ? "true" : "false");
        fprintf(f, "  \"cached_search_count\": %d,\n",    st->cached_search_count);
        fprintf(f, "  \"cached_search\": [\n");
        for (int i = 0; i < st->cached_search_count; i++) {
            char *et = json_escape_string(st->cached_search[i].title);
            char *ev = json_escape_string(st->cached_search[i].video_id);
            char *eu = json_escape_string(st->cached_search[i].url);
            fprintf(f, "    {\"title\": \"%s\", \"video_id\": \"%s\", \"url\": \"%s\", \"duration\": %d}%s\n",
                    et ? et : "", ev ? ev : "", eu ? eu : "",
                    st->cached_search[i].duration,
                    (i < st->cached_search_count - 1) ? "," : "");
            free(et); free(ev); free(eu);
        }
        fprintf(f, "  ]\n");
    } else {
        fprintf(f, "  \"last_query\": \"\",\n");
        fprintf(f, "  \"cached_search_count\": 0,\n");
        fprintf(f, "  \"cached_search\": []\n");
    }
    fprintf(f, "}\n");

    free(esc_path);
    free(esc_query);
    fclose(f);
}

void load_config(AppState *st) {
    init_default_config(st);
    st->last_query[0]       = '\0';
    st->cached_search_count = 0;
    st->last_playlist_idx   = -1;
    st->last_song_idx       = -1;
    st->was_playing_playlist = false;

    FILE *f = fopen(st->config_file, "r");
    if (!f) { save_config(st); return; }

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fsize <= 0 || fsize > 256 * 1024) { fclose(f); return; }

    char *content = malloc((size_t)fsize + 1);
    if (!content) { fclose(f); return; }

    size_t n = fread(content, 1, (size_t)fsize, f);
    content[n] = '\0';
    fclose(f);

    char *dp = json_get_string(content, "download_path");
    if (dp && dp[0]) {
        snprintf(st->config.download_path, sizeof(st->config.download_path), "%s", dp);
    }
    free(dp);

    st->config.seek_step = json_get_int(content, "seek_step", 10);
    if (st->config.seek_step < 1 || st->config.seek_step > 300)
        st->config.seek_step = 10;

    st->config.remember_session = json_get_bool(content, "remember_session", false);
    st->config.shuffle_mode     = json_get_bool(content, "shuffle_mode",     false);

    char *lq = json_get_string(content, "last_query");
    if (lq) {
        snprintf(st->last_query, sizeof(st->last_query), "%s", lq);
        free(lq);
    }

    st->last_playlist_idx    = json_get_int(content,  "last_playlist_idx",    -1);
    st->last_song_idx        = json_get_int(content,  "last_song_idx",        -1);
    st->was_playing_playlist = json_get_bool(content, "was_playing_playlist", false);

    st->cached_search_count = json_get_int(content, "cached_search_count", 0);
    if (st->cached_search_count > MAX_RESULTS) st->cached_search_count = MAX_RESULTS;

    if (st->cached_search_count > 0) {
        const char *arr = strstr(content, "\"cached_search\"");
        if (arr) arr = strchr(arr, '[');
        if (arr) {
            arr++;
            for (int i = 0; i < st->cached_search_count; i++) {
                const char *os = strchr(arr, '{');
                if (!os) break;
                const char *oe = strchr(os, '}');
                if (!oe) break;

                size_t ol = (size_t)(oe - os) + 1;
                char *obj = malloc(ol + 1);
                if (!obj) break;
                memcpy(obj, os, ol);
                obj[ol] = '\0';

                st->cached_search[i].title    = json_get_string(obj, "title");
                st->cached_search[i].video_id = json_get_string(obj, "video_id");
                st->cached_search[i].url      = json_get_string(obj, "url");
                st->cached_search[i].duration = json_get_int(obj, "duration", 0);
                free(obj);
                arr = oe + 1;
            }
        }
    }

    free(content);
}

// ============================================================================
// yt-dlp binary selection
// ============================================================================

const char *get_ytdlp_cmd(AppState *st) {
    if (st->ytdlp_has_local && file_exists(st->ytdlp_local_path))
        return st->ytdlp_local_path;
    return "yt-dlp";
}
