#ifndef UTILS_H
#define UTILS_H

#include <stdbool.h>
#include <stddef.h>

#include "types.h"

// File / directory helpers
bool file_exists(const char *path);
bool dir_exists(const char *path);
bool mkdir_p(const char *path);
bool delete_directory_recursive(const char *path);

// String helpers
char *trim_whitespace(char *s);

// Truncates buf in-place to max_len chars, appending "..." if shortened.
// buf must hold at least max_len+1 bytes; max_len must be >= 3.
void truncate_str(char *buf, int max_len);

// Filename helpers
char *sanitize_filename(const char *name);   // returns malloc'd "name.json"
void  sanitize_title_for_filename(const char *title, const char *video_id,
                                   char *out, size_t out_size);

// Song file helpers
bool file_exists_for_video(const char *dir_path, const char *video_id);
bool get_local_file_path_for_song(const AppState *st, const char *playlist_name,
                                   const char *video_id,
                                   char *out_path, size_t out_size);

// JSON helpers (minimal hand-rolled parser)
char *json_escape_string(const char *s);   // caller must free
char *json_get_string(const char *json, const char *key);  // caller must free
int   json_get_int(const char *json, const char *key, int default_val);
bool  json_get_bool(const char *json, const char *key, bool default_val);

// Formatting
void format_duration(int sec, char out[16]);

#endif /* UTILS_H */
