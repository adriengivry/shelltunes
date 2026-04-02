#ifndef PLAYBACK_H
#define PLAYBACK_H

#include <stdbool.h>

#include "types.h"

bool mpv_is_connected(void);
void mpv_start_if_needed(AppState *st);
void mpv_quit(void);

void mpv_toggle_pause(void);
void mpv_stop_playback(void);
void mpv_seek(int seconds);
void mpv_seek_absolute(int seconds);
void mpv_load_url(const char *url);

// Returns true when a track has genuinely ended (EOF).
bool mpv_check_track_end(void);

void play_search_result(AppState *st, int idx);
void play_playlist_song(AppState *st, int playlist_idx, int song_idx);
void play_next(AppState *st);
void play_prev(AppState *st);

#endif /* PLAYBACK_H */
