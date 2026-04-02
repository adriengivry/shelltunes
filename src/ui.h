#ifndef UI_H
#define UI_H

#include <stddef.h>

#include "types.h"

// Context passed to the YouTube playlist fetch progress callback.
typedef struct {
    AppState *st;
    char     *status;
    size_t    status_size;
} YTProgressCtx;

// Progress callback suitable for fetch_youtube_playlist().
void yt_progress_cb(int count, const char *msg, void *user_data);

// Full-screen redraw.
void draw_ui(AppState *st, const char *status);

// Prompt user for a single-line string at the bottom of the screen.
// Returns the length of the trimmed input (0 if empty/cancelled).
int get_string_input(char *buf, size_t bufsz, const char *prompt);

// Modal help screen (blocks until any key is pressed).
void show_help(void);

// Returns true if all required system dependencies are present.
// On failure, writes an error description into errmsg.
bool check_dependencies(AppState *st, char *errmsg, size_t errsz);

// Show the exit confirmation dialog when downloads are pending.
void draw_exit_dialog_pub(AppState *st, int pending_count);

#endif /* UI_H */
