#ifndef DOWNLOAD_H
#define DOWNLOAD_H

#include "types.h"

void save_download_queue(AppState *st);   // call with mutex held
void load_download_queue(AppState *st);

void start_download_thread(AppState *st);
void stop_download_thread(AppState *st);

// Returns: 1 = queued, 0 = already done/queued, -1 = error
int add_to_download_queue(AppState *st, const char *video_id,
                           const char *title, const char *playlist_name);

int get_pending_download_count(AppState *st);

#endif /* DOWNLOAD_H */
