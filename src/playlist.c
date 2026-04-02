#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "playlist.h"
#include "download.h"
#include "utils.h"

// ============================================================================
// Memory management
// ============================================================================

void free_playlist_items(Playlist *pl) {
    for (int i = 0; i < pl->count; i++) {
        free(pl->items[i].title);
        free(pl->items[i].video_id);
        free(pl->items[i].url);
        pl->items[i].title    = NULL;
        pl->items[i].video_id = NULL;
        pl->items[i].url      = NULL;
    }
    pl->count = 0;
}

void free_playlist(Playlist *pl) {
    free(pl->name);
    free(pl->filename);
    pl->name     = NULL;
    pl->filename = NULL;
    free_playlist_items(pl);
}

void free_all_playlists(AppState *st) {
    for (int i = 0; i < st->playlist_count; i++)
        free_playlist(&st->playlists[i]);
    st->playlist_count = 0;
}

// ============================================================================
// Persistence
// ============================================================================

void save_playlists_index(AppState *st) {
    FILE *f = fopen(st->playlists_index, "w");
    if (!f) return;

    fprintf(f, "{\n  \"playlists\": [\n");
    for (int i = 0; i < st->playlist_count; i++) {
        char *en = json_escape_string(st->playlists[i].name);
        char *ef = json_escape_string(st->playlists[i].filename);
        fprintf(f, "    {\"name\": \"%s\", \"filename\": \"%s\"}%s\n",
                en ? en : "", ef ? ef : "",
                (i < st->playlist_count - 1) ? "," : "");
        free(en); free(ef);
    }
    fprintf(f, "  ]\n}\n");
    fclose(f);
}

void save_playlist(AppState *st, int idx) {
    if (idx < 0 || idx >= st->playlist_count) return;
    Playlist *pl = &st->playlists[idx];

    char path[4096];
    snprintf(path, sizeof(path), "%s/%s", st->playlists_dir, pl->filename);

    FILE *f = fopen(path, "w");
    if (!f) return;

    fprintf(f, "{\n  \"name\": \"%s\",\n  \"type\": \"%s\",\n  \"songs\": [\n",
            pl->name, pl->is_youtube_playlist ? "youtube" : "local");

    for (int i = 0; i < pl->count; i++) {
        char *et = json_escape_string(pl->items[i].title);
        char *ev = json_escape_string(pl->items[i].video_id);
        fprintf(f, "    {\"title\": \"%s\", \"video_id\": \"%s\", \"duration\": %d}%s\n",
                et ? et : "", ev ? ev : "",
                pl->items[i].duration,
                (i < pl->count - 1) ? "," : "");
        free(et); free(ev);
    }
    fprintf(f, "  ]\n}\n");
    fclose(f);
}

void load_playlist_songs(AppState *st, int idx) {
    if (idx < 0 || idx >= st->playlist_count) return;
    Playlist *pl = &st->playlists[idx];
    free_playlist_items(pl);

    char path[4096];
    snprintf(path, sizeof(path), "%s/%s", st->playlists_dir, pl->filename);

    FILE *f = fopen(path, "r");
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

    char *type = json_get_string(content, "type");
    pl->is_youtube_playlist = (type && strcmp(type, "youtube") == 0);
    free(type);

    const char *p = strstr(content, "\"songs\"");
    if (!p) { free(content); return; }
    p = strchr(p, '[');
    if (!p) { free(content); return; }

    while (pl->count < MAX_PLAYLIST_ITEMS) {
        const char *os = strchr(p, '{');
        if (!os) break;
        const char *oe = strchr(os, '}');
        if (!oe) break;

        size_t ol = (size_t)(oe - os) + 1;
        char *obj = malloc(ol + 1);
        if (!obj) break;
        memcpy(obj, os, ol);
        obj[ol] = '\0';

        char *title    = json_get_string(obj, "title");
        char *video_id = json_get_string(obj, "video_id");
        int   duration = json_get_int(obj, "duration", 0);
        free(obj);

        if (title && video_id && video_id[0]) {
            pl->items[pl->count].title    = title;
            pl->items[pl->count].video_id = video_id;
            pl->items[pl->count].duration = duration;

            char url[256];
            snprintf(url, sizeof(url), "https://www.youtube.com/watch?v=%s", video_id);
            pl->items[pl->count].url = strdup(url);
            pl->count++;
        } else {
            free(title);
            free(video_id);
        }
        p = oe + 1;
    }
    free(content);
}

void load_playlists(AppState *st) {
    free_all_playlists(st);

    FILE *f = fopen(st->playlists_index, "r");
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

    const char *p = strstr(content, "\"playlists\"");
    if (!p) { free(content); return; }
    p = strchr(p, '[');
    if (!p) { free(content); return; }

    while (st->playlist_count < MAX_PLAYLISTS) {
        const char *os = strchr(p, '{');
        if (!os) break;
        const char *oe = strchr(os, '}');
        if (!oe) break;

        size_t ol = (size_t)(oe - os) + 1;
        char *obj = malloc(ol + 1);
        if (!obj) break;
        memcpy(obj, os, ol);
        obj[ol] = '\0';

        char *name     = json_get_string(obj, "name");
        char *filename = json_get_string(obj, "filename");
        free(obj);

        if (name && filename && name[0] && filename[0]) {
            st->playlists[st->playlist_count].name     = name;
            st->playlists[st->playlist_count].filename = filename;
            st->playlists[st->playlist_count].count    = 0;
            st->playlist_count++;
        } else {
            free(name);
            free(filename);
        }
        p = oe + 1;
    }
    free(content);
}

// ============================================================================
// CRUD
// ============================================================================

int create_playlist(AppState *st, const char *name, bool is_youtube) {
    if (st->playlist_count >= MAX_PLAYLISTS) return -1;
    if (!name || !name[0]) return -1;

    for (int i = 0; i < st->playlist_count; i++)
        if (strcasecmp(st->playlists[i].name, name) == 0) return -2;

    char *filename = sanitize_filename(name);
    if (!filename) return -1;

    // Deduplicate filename
    for (int i = 0; i < st->playlist_count; i++) {
        if (strcmp(st->playlists[i].filename, filename) == 0) {
            char *nf = malloc(strlen(filename) + 10);
            if (!nf) { free(filename); return -1; }
            snprintf(nf, strlen(filename) + 10, "%d_%s", st->playlist_count, filename);
            free(filename);
            filename = nf;
            break;
        }
    }

    int idx = st->playlist_count;
    st->playlists[idx].name              = strdup(name);
    st->playlists[idx].filename          = filename;
    st->playlists[idx].count             = 0;
    st->playlists[idx].is_youtube_playlist = is_youtube;
    st->playlist_count++;

    save_playlists_index(st);
    save_playlist(st, idx);
    return idx;
}

bool delete_playlist(AppState *st, int idx) {
    if (idx < 0 || idx >= st->playlist_count) return false;

    char playlist_name[256];
    snprintf(playlist_name, sizeof(playlist_name), "%s", st->playlists[idx].name);

    char path[4096];
    snprintf(path, sizeof(path), "%s/%s", st->playlists_dir, st->playlists[idx].filename);
    unlink(path);

    char dl_dir[4096];
    snprintf(dl_dir, sizeof(dl_dir), "%s/%s", st->config.download_path, playlist_name);
    if (dir_exists(dl_dir)) delete_directory_recursive(dl_dir);

    free_playlist(&st->playlists[idx]);

    for (int i = idx; i < st->playlist_count - 1; i++)
        st->playlists[i] = st->playlists[i + 1];
    st->playlist_count--;
    memset(&st->playlists[st->playlist_count], 0, sizeof(Playlist));

    save_playlists_index(st);
    return true;
}

bool add_song_to_playlist(AppState *st, int playlist_idx, Song *song) {
    if (playlist_idx < 0 || playlist_idx >= st->playlist_count) return false;
    if (!song || !song->video_id) return false;

    Playlist *pl = &st->playlists[playlist_idx];

    if (pl->count == 0 && file_exists(st->playlists_dir))
        load_playlist_songs(st, playlist_idx);

    if (pl->count >= MAX_PLAYLIST_ITEMS) return false;

    for (int i = 0; i < pl->count; i++)
        if (pl->items[i].video_id && strcmp(pl->items[i].video_id, song->video_id) == 0)
            return false;

    int idx = pl->count;
    pl->items[idx].title    = strdup(song->title ? song->title : "Unknown");
    pl->items[idx].video_id = strdup(song->video_id);
    pl->items[idx].duration = song->duration;

    char url[256];
    snprintf(url, sizeof(url), "https://www.youtube.com/watch?v=%s", song->video_id);
    pl->items[idx].url = strdup(url);
    pl->count++;

    save_playlist(st, playlist_idx);
    add_to_download_queue(st, song->video_id, song->title, pl->name);
    return true;
}

bool remove_song_from_playlist(AppState *st, int playlist_idx, int song_idx) {
    if (playlist_idx < 0 || playlist_idx >= st->playlist_count) return false;
    Playlist *pl = &st->playlists[playlist_idx];
    if (song_idx < 0 || song_idx >= pl->count) return false;

    free(pl->items[song_idx].title);
    free(pl->items[song_idx].video_id);
    free(pl->items[song_idx].url);

    for (int i = song_idx; i < pl->count - 1; i++)
        pl->items[i] = pl->items[i + 1];
    pl->count--;
    memset(&pl->items[pl->count], 0, sizeof(Song));

    save_playlist(st, playlist_idx);
    return true;
}
