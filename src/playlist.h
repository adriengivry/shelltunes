#ifndef PLAYLIST_H
#define PLAYLIST_H

#include <stdbool.h>

#include "types.h"

void free_playlist_items(Playlist *pl);
void free_playlist(Playlist *pl);
void free_all_playlists(AppState *st);

void save_playlists_index(AppState *st);
void save_playlist(AppState *st, int idx);
void load_playlist_songs(AppState *st, int idx);
void load_playlists(AppState *st);

// Returns playlist index on success, -1 on error, -2 if name already exists.
int  create_playlist(AppState *st, const char *name, bool is_youtube);
bool delete_playlist(AppState *st, int idx);

bool add_song_to_playlist(AppState *st, int playlist_idx, Song *song);
bool remove_song_from_playlist(AppState *st, int playlist_idx, int song_idx);

#endif /* PLAYLIST_H */
