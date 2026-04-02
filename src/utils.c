#define _GNU_SOURCE

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "utils.h"

// ============================================================================
// File / directory helpers
// ============================================================================

bool file_exists(const char *path) {
    struct stat sb;
    return stat(path, &sb) == 0;
}

bool dir_exists(const char *path) {
    struct stat sb;
    return stat(path, &sb) == 0 && S_ISDIR(sb.st_mode);
}

bool mkdir_p(const char *path) {
    char tmp[4096];
    snprintf(tmp, sizeof(tmp), "%s", path);
    size_t len = strlen(tmp);

    if (len > 0 && tmp[len - 1] == '/')
        tmp[--len] = '\0';

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (!dir_exists(tmp) && mkdir(tmp, 0755) != 0 && errno != EEXIST)
                return false;
            *p = '/';
        }
    }
    if (!dir_exists(tmp) && mkdir(tmp, 0755) != 0 && errno != EEXIST)
        return false;
    return true;
}

bool delete_directory_recursive(const char *path) {
    DIR *dir = opendir(path);
    if (!dir) return false;

    struct dirent *entry;
    char filepath[4096];
    bool success = true;

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        snprintf(filepath, sizeof(filepath), "%s/%s", path, entry->d_name);

        struct stat st;
        if (stat(filepath, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                if (!delete_directory_recursive(filepath)) success = false;
            } else {
                if (unlink(filepath) != 0) success = false;
            }
        }
    }
    closedir(dir);
    if (rmdir(path) != 0) success = false;
    return success;
}

// ============================================================================
// String helpers
// ============================================================================

char *trim_whitespace(char *s) {
    if (!s) return s;
    while (*s && isspace((unsigned char)*s)) s++;
    if (*s == '\0') return s;
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return s;
}

void truncate_str(char *buf, int max_len) {
    if (max_len < 3) return;
    if ((int)strlen(buf) > max_len) {
        buf[max_len - 3] = '.';
        buf[max_len - 2] = '.';
        buf[max_len - 1] = '.';
        buf[max_len]     = '\0';
    }
}

// ============================================================================
// Filename helpers
// ============================================================================

char *sanitize_filename(const char *name) {
    size_t len = strlen(name);
    char *out = malloc(len + 6); // ".json" + '\0'
    if (!out) return NULL;

    size_t j = 0;
    for (size_t i = 0; i < len && j < len; i++) {
        char c = name[i];
        if (isalnum((unsigned char)c) || c == '-' || c == '_')
            out[j++] = (char)tolower((unsigned char)c);
        else if (c == ' ')
            out[j++] = '_';
    }
    out[j] = '\0';
    strcat(out, ".json");
    return out;
}

void sanitize_title_for_filename(const char *title, const char *video_id,
                                  char *out, size_t out_size) {
    if (!title || !video_id || !out || out_size < 32) {
        if (out && out_size > 0) out[0] = '\0';
        return;
    }

    char sanitized[256] = {0};
    size_t j = 0;

    for (size_t i = 0; title[i] && j < sizeof(sanitized) - 1; i++) {
        char c = title[i];
        if (c == '/' || c == '\\' || c == ':' || c == '*' ||
            c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
            continue;
        } else if (c == ' ' || c == '\'' || c == '`') {
            if (j > 0 && sanitized[j - 1] != '_')
                sanitized[j++] = '_';
        } else if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' ||
                   (unsigned char)c > 127) {
            sanitized[j++] = c;
        }
    }

    while (j > 0 && sanitized[j - 1] == '_') j--;
    sanitized[j] = '\0';

    if (sanitized[0] == '\0') strcpy(sanitized, "download");
    if (strlen(sanitized) > 180) sanitized[180] = '\0';

    snprintf(out, out_size, "%s_[%s].mp3", sanitized, video_id);
}

// ============================================================================
// Song file helpers
// ============================================================================

bool file_exists_for_video(const char *dir_path, const char *video_id) {
    DIR *dir = opendir(dir_path);
    if (!dir) return false;

    char pattern[64];
    snprintf(pattern, sizeof(pattern), "[%s].mp3", video_id);

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strstr(entry->d_name, pattern)) {
            closedir(dir);
            return true;
        }
    }
    closedir(dir);
    return false;
}

bool get_local_file_path_for_song(const AppState *st, const char *playlist_name,
                                   const char *video_id,
                                   char *out_path, size_t out_size) {
    if (!video_id || !video_id[0] || !out_path || out_size == 0) return false;

    char dest_dir[4096];
    if (playlist_name && playlist_name[0])
        snprintf(dest_dir, sizeof(dest_dir), "%s/%s", st->config.download_path, playlist_name);
    else
        snprintf(dest_dir, sizeof(dest_dir), "%s", st->config.download_path);

    DIR *dir = opendir(dest_dir);
    if (!dir) return false;

    char pattern[64];
    snprintf(pattern, sizeof(pattern), "[%s].mp3", video_id);

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strstr(entry->d_name, pattern)) {
            snprintf(out_path, out_size, "%s/%s", dest_dir, entry->d_name);
            closedir(dir);
            return true;
        }
    }
    closedir(dir);
    return false;
}

// ============================================================================
// JSON helpers
// ============================================================================

char *json_escape_string(const char *s) {
    if (!s) return strdup("");

    size_t len = strlen(s);
    size_t alloc = len * 2 + 1;
    char *out = malloc(alloc);
    if (!out) return NULL;

    size_t j = 0;
    for (size_t i = 0; i < len && j < alloc - 2; i++) {
        char c = s[i];
        if (c == '"' || c == '\\') {
            out[j++] = '\\';
            out[j++] = c;
        } else if (c == '\n') { out[j++] = '\\'; out[j++] = 'n'; }
        else if (c == '\r')   { out[j++] = '\\'; out[j++] = 'r'; }
        else if (c == '\t')   { out[j++] = '\\'; out[j++] = 't'; }
        else                  { out[j++] = c; }
    }
    out[j] = '\0';
    return out;
}

char *json_get_string(const char *json, const char *key) {
    char pattern[256];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);

    const char *p = strstr(json, pattern);
    if (!p) return NULL;

    p += strlen(pattern);
    while (*p && (*p == ' ' || *p == ':' || *p == '\t')) p++;
    if (*p != '"') return NULL;
    p++;

    const char *start = p;
    while (*p && *p != '"') {
        if (*p == '\\' && *(p + 1)) p++;
        p++;
    }

    size_t len = (size_t)(p - start);
    char *result = malloc(len + 1);
    if (!result) return NULL;

    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        if (start[i] == '\\' && i + 1 < len) {
            i++;
            if      (start[i] == 'n') result[j++] = '\n';
            else if (start[i] == 'r') result[j++] = '\r';
            else if (start[i] == 't') result[j++] = '\t';
            else                      result[j++] = start[i];
        } else {
            result[j++] = start[i];
        }
    }
    result[j] = '\0';
    return result;
}

int json_get_int(const char *json, const char *key, int default_val) {
    char pattern[256];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);

    const char *p = strstr(json, pattern);
    if (!p) return default_val;

    p += strlen(pattern);
    while (*p && (*p == ' ' || *p == ':' || *p == '\t')) p++;
    if (!*p) return default_val;

    if (strncmp(p, "true",  4) == 0) return 1;
    if (strncmp(p, "false", 5) == 0) return 0;
    return atoi(p);
}

bool json_get_bool(const char *json, const char *key, bool default_val) {
    return json_get_int(json, key, default_val ? 1 : 0) != 0;
}

// ============================================================================
// Formatting
// ============================================================================

void format_duration(int sec, char out[16]) {
    if (sec <= 0) {
        snprintf(out, 16, "--:--");
        return;
    }
    int h = sec / 3600;
    int m = (sec % 3600) / 60;
    int s = sec % 60;
    if (h > 0)
        snprintf(out, 16, "%d:%02d:%02d", h, m, s);
    else
        snprintf(out, 16, "%02d:%02d", m, s);
}
