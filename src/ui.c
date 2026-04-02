#define _GNU_SOURCE

#include <ncurses.h>
#include <string.h>

#include "ui.h"
#include "download.h"
#include "playlist.h"
#include "utils.h"

// ============================================================================
// YouTube progress callback
// ============================================================================

void yt_progress_cb(int count, const char *msg, void *user_data) {
    (void)count;
    YTProgressCtx *ctx = user_data;
    if (!ctx || !msg) return;
    snprintf(ctx->status, ctx->status_size, "%s", msg);
    draw_ui(ctx->st, ctx->status);
    refresh();
}

// ============================================================================
// Internal helpers
// ============================================================================

static void format_title(const char *title, char *buf, size_t bufsz, int max_cols) {
    snprintf(buf, bufsz, "%s", title ? title : "(no title)");
    if (max_cols >= 3) truncate_str(buf, max_cols);
}

static char get_spinner_char(int frame) {
    const char spinner[] = {'|', '/', '-', '\\'};
    return spinner[frame % 4];
}

// ============================================================================
// Header
// ============================================================================

static void draw_header(int cols, ViewMode view) {
    attron(A_BOLD);
    mvprintw(0, 0, " ShellBeats v0.6 ");
    attroff(A_BOLD);

    switch (view) {
        case VIEW_SEARCH:
            mvprintw(1, 0, "  /,s: search | Enter: play | Space: pause | n/p: next/prev | R: shuffle | t: jump");
            mvprintw(2, 0, "  Left/Right: seek | a: add | d: download | f: playlists | S: settings | q: quit");
            break;
        case VIEW_PLAYLISTS:
            mvprintw(1, 0, "  Enter: open | c: create | e: rename | p: add YouTube | x: delete | d: download all");
            mvprintw(2, 0, "  Esc: back | i: about | q: quit");
            break;
        case VIEW_PLAYLIST_SONGS:
            mvprintw(1, 0, "  Enter: play | Space: pause | n/p: next/prev | R: shuffle | t: jump | Left/Right: seek");
            mvprintw(2, 0, "  a: add | d: download | r: remove | D: download all | u: sync YT | Esc: back | q: quit");
            break;
        case VIEW_ADD_TO_PLAYLIST:
            mvprintw(1, 0, "  Enter: add to playlist | c: create new playlist");
            mvprintw(2, 0, "  Esc: cancel");
            break;
        case VIEW_SETTINGS:
            mvprintw(1, 0, "  Up/Down: navigate | Enter: edit/toggle");
            mvprintw(2, 0, "  Esc: back | i: about | q: quit");
            break;
        case VIEW_ABOUT:
            mvprintw(1, 0, "  Press any key to close");
            move(2, 0);
            break;
    }

    mvhline(3, 0, ACS_HLINE, cols);
}

// ============================================================================
// Download status bar
// ============================================================================

static void draw_download_status(AppState *st, int rows, int cols) {
    char dl_status[128] = {0};
    char spinner = get_spinner_char(st->spinner_frame);
    int  parts   = 0;

    if (st->ytdlp_updating) {
        snprintf(dl_status, sizeof(dl_status), "[%c Fetching updates...]", spinner);
        parts++;
    }

    pthread_mutex_lock(&st->download_queue.mutex);
    int pending = 0;
    int completed = st->download_queue.completed;
    int failed    = st->download_queue.failed;
    for (int i = 0; i < st->download_queue.count; i++) {
        DownloadStatus s = st->download_queue.tasks[i].status;
        if (s == DOWNLOAD_PENDING || s == DOWNLOAD_ACTIVE) pending++;
    }
    pthread_mutex_unlock(&st->download_queue.mutex);

    if (pending > 0) {
        char qs[64];
        if (failed > 0)
            snprintf(qs, sizeof(qs), "[%c %d/%d %d!]", spinner, completed, completed + pending, failed);
        else
            snprintf(qs, sizeof(qs), "[%c %d/%d]", spinner, completed, completed + pending);

        if (parts > 0) {
            size_t cur = strlen(dl_status);
            snprintf(dl_status + cur, sizeof(dl_status) - cur, " %s", qs);
        } else {
            snprintf(dl_status, sizeof(dl_status), "%s", qs);
        }
        parts++;
    }

    if (parts == 0) return;
    int x = cols - (int)strlen(dl_status) - 1;
    if (x > 0) mvprintw(rows - 1, x, "%s", dl_status);
}

// ============================================================================
// Now playing bar
// ============================================================================

static void draw_now_playing(AppState *st, int rows, int cols) {
    mvhline(rows - 2, 0, ACS_HLINE, cols);

    const char *title = NULL;
    if (st->playing_from_playlist && st->playing_playlist_idx >= 0 &&
        st->playing_playlist_idx < st->playlist_count) {
        Playlist *pl = &st->playlists[st->playing_playlist_idx];
        if (st->playing_index >= 0 && st->playing_index < pl->count)
            title = pl->items[st->playing_index].title;
    } else if (st->playing_index >= 0 && st->playing_index < st->search_count) {
        title = st->search_results[st->playing_index].title;
    }

    if (title) {
        mvprintw(rows - 1, 0, " Now playing: ");
        attron(A_BOLD);
        int max_np = cols - 35;
        char npbuf[512];
        format_title(title, npbuf, sizeof(npbuf), max_np);
        printw("%s", npbuf);
        attroff(A_BOLD);
        if (st->paused)                 printw(" [PAUSED]");
        if (st->config.shuffle_mode)    printw(" [SHUFFLE]");
    }

    draw_download_status(st, rows, cols);
}

// ============================================================================
// View drawing
// ============================================================================

static void draw_search_view(AppState *st, const char *status, int rows, int cols) {
    mvprintw(4, 0, "Query: ");
    attron(A_BOLD);
    printw("%s", st->query[0] ? st->query : "(none)");
    attroff(A_BOLD);
    mvprintw(4, cols - 20, "Results: %d", st->search_count);

    if (status && status[0]) mvprintw(5, 0, ">>> %s", status);
    mvhline(6, 0, ACS_HLINE, cols);

    int list_top    = 7;
    int list_height = rows - list_top - 2;
    if (list_height < 1) list_height = 1;

    if (st->search_selected < st->search_scroll)
        st->search_scroll = st->search_selected;
    else if (st->search_selected >= st->search_scroll + list_height)
        st->search_scroll = st->search_selected - list_height + 1;

    for (int i = 0; i < list_height && (st->search_scroll + i) < st->search_count; i++) {
        int  idx         = st->search_scroll + i;
        bool is_selected = (idx == st->search_selected);
        bool is_playing  = (!st->playing_from_playlist && idx == st->playing_index);

        int y = list_top + i;
        move(y, 0);
        clrtoeol();

        char mark = ' ';
        if (is_playing) { mark = st->paused ? '|' : '>'; attron(A_BOLD); }
        if (is_selected) attron(A_REVERSE);

        char dur[16];
        format_duration(st->search_results[idx].duration, dur);

        char local_path[2048];
        bool dl = get_local_file_path_for_song(st, NULL,
                      st->search_results[idx].video_id, local_path, sizeof(local_path));
        const char *dl_mark = dl ? "[D]" : "   ";

        int max_title = cols - 20;
        if (max_title < 20) max_title = 20;

        char titlebuf[1024];
        format_title(st->search_results[idx].title, titlebuf, sizeof(titlebuf), max_title);
        mvprintw(y, 0, " %c %3d. %s [%s] %s", mark, idx + 1, dl_mark, dur, titlebuf);

        if (is_selected) attroff(A_REVERSE);
        if (is_playing)  attroff(A_BOLD);
    }
}

static void draw_playlists_view(AppState *st, const char *status, int rows, int cols) {
    mvprintw(4, 0, "Playlists");
    mvprintw(4, cols - 20, "Total: %d", st->playlist_count);
    if (status && status[0]) mvprintw(5, 0, ">>> %s", status);
    mvhline(6, 0, ACS_HLINE, cols);

    int list_top    = 7;
    int list_height = rows - list_top - 2;
    if (list_height < 1) list_height = 1;

    if (st->playlist_count == 0) {
        mvprintw(list_top + 1, 2, "No playlists yet. Press 'c' to create one.");
        return;
    }

    if (st->playlist_selected < st->playlist_scroll)
        st->playlist_scroll = st->playlist_selected;
    else if (st->playlist_selected >= st->playlist_scroll + list_height)
        st->playlist_scroll = st->playlist_selected - list_height + 1;

    for (int i = 0; i < list_height && (st->playlist_scroll + i) < st->playlist_count; i++) {
        int  idx         = st->playlist_scroll + i;
        bool is_selected = (idx == st->playlist_selected);

        int y = list_top + i;
        move(y, 0);
        clrtoeol();
        if (is_selected) attron(A_REVERSE);

        Playlist *pl = &st->playlists[idx];
        if (pl->count == 0) load_playlist_songs(st, idx);
        mvprintw(y, 0, "   %3d. %s (%d songs)", idx + 1, pl->name, pl->count);

        if (is_selected) attroff(A_REVERSE);
    }
}

static void draw_playlist_songs_view(AppState *st, const char *status, int rows, int cols) {
    if (st->current_playlist_idx < 0 || st->current_playlist_idx >= st->playlist_count) return;

    Playlist *pl = &st->playlists[st->current_playlist_idx];

    mvprintw(4, 0, "Playlist: ");
    attron(A_BOLD);
    printw("%s", pl->name);
    if (pl->is_youtube_playlist) printw(" [YT]");
    attroff(A_BOLD);
    mvprintw(4, cols - 20, "Songs: %d", pl->count);

    if (status && status[0]) mvprintw(5, 0, ">>> %s", status);
    mvhline(6, 0, ACS_HLINE, cols);

    int list_top    = 7;
    int list_height = rows - list_top - 2;
    if (list_height < 1) list_height = 1;

    if (pl->count == 0) {
        mvprintw(list_top + 1, 2, "Playlist is empty. Search for songs and press 'a' to add.");
        return;
    }

    if (st->playlist_song_selected < st->playlist_song_scroll)
        st->playlist_song_scroll = st->playlist_song_selected;
    else if (st->playlist_song_selected >= st->playlist_song_scroll + list_height)
        st->playlist_song_scroll = st->playlist_song_selected - list_height + 1;

    for (int i = 0; i < list_height && (st->playlist_song_scroll + i) < pl->count; i++) {
        int  idx         = st->playlist_song_scroll + i;
        bool is_selected = (idx == st->playlist_song_selected);
        bool is_playing  = (st->playing_from_playlist &&
                            st->playing_playlist_idx == st->current_playlist_idx &&
                            st->playing_index == idx);

        int y = list_top + i;
        move(y, 0);
        clrtoeol();

        char mark = ' ';
        if (is_playing) { mark = st->paused ? '|' : '>'; attron(A_BOLD); }
        if (is_selected) attron(A_REVERSE);

        char dur[16];
        format_duration(pl->items[idx].duration, dur);

        char local_path[2048];
        bool dl = get_local_file_path_for_song(st, pl->name,
                      pl->items[idx].video_id, local_path, sizeof(local_path));
        const char *dl_mark = dl ? "[D]" : "   ";

        int max_title = cols - 20;
        if (max_title < 20) max_title = 20;

        char titlebuf[1024];
        format_title(pl->items[idx].title, titlebuf, sizeof(titlebuf), max_title);
        mvprintw(y, 0, " %c %3d. %s [%s] %s", mark, idx + 1, dl_mark, dur, titlebuf);

        if (is_selected) attroff(A_REVERSE);
        if (is_playing)  attroff(A_BOLD);
    }
}

static void draw_add_to_playlist_view(AppState *st, const char *status, int rows, int cols) {
    mvprintw(2, 0, "Add to playlist: ");
    if (st->song_to_add && st->song_to_add->title) {
        attron(A_BOLD);
        int max_title = cols - 20;
        char titlebuf[256];
        format_title(st->song_to_add->title, titlebuf, sizeof(titlebuf), max_title);
        printw("%s", titlebuf);
        attroff(A_BOLD);
    }

    if (status && status[0]) mvprintw(5, 0, ">>> %s", status);
    mvhline(6, 0, ACS_HLINE, cols);

    int list_top    = 7;
    int list_height = rows - list_top - 2;
    if (list_height < 1) list_height = 1;

    if (st->playlist_count == 0) {
        mvprintw(list_top + 1, 2, "No playlists yet. Press 'c' to create one.");
        return;
    }

    if (st->add_to_playlist_selected < st->add_to_playlist_scroll)
        st->add_to_playlist_scroll = st->add_to_playlist_selected;
    else if (st->add_to_playlist_selected >= st->add_to_playlist_scroll + list_height)
        st->add_to_playlist_scroll = st->add_to_playlist_selected - list_height + 1;

    for (int i = 0; i < list_height && (st->add_to_playlist_scroll + i) < st->playlist_count; i++) {
        int  idx         = st->add_to_playlist_scroll + i;
        bool is_selected = (idx == st->add_to_playlist_selected);

        int y = list_top + i;
        move(y, 0);
        clrtoeol();
        if (is_selected) attron(A_REVERSE);

        Playlist *pl = &st->playlists[idx];
        mvprintw(y, 0, "   %3d. %s (%d songs)", idx + 1, pl->name, pl->count);

        if (is_selected) attroff(A_REVERSE);
    }
}

static void draw_settings_view(AppState *st, const char *status, int rows, int cols) {
    (void)rows;
    mvprintw(4, 0, "Settings");
    if (status && status[0]) mvprintw(5, 0, ">>> %s", status);
    mvhline(6, 0, ACS_HLINE, cols);

    int y = 8;

    // Setting 0: Download Path
    bool is_selected = (st->settings_selected == 0);
    mvprintw(y, 2, "Download Path:");
    y++;

    if (is_selected) attron(A_REVERSE);

    if (st->settings_editing && is_selected) {
        mvprintw(y, 4, "%-*s", cols - 8, st->settings_edit_buffer);
        move(y, 4 + st->settings_edit_pos);
        curs_set(1);
    } else {
        int max_path = cols - 8;
        char pathbuf[1024];
        snprintf(pathbuf, sizeof(pathbuf), "%s", st->config.download_path);
        if ((int)strlen(pathbuf) > max_path && max_path > 3) {
            int offset = (int)strlen(pathbuf) - max_path + 3;
            memmove(pathbuf + 3, pathbuf + offset, strlen(pathbuf) - offset + 1);
            pathbuf[0] = pathbuf[1] = pathbuf[2] = '.';
        }
        mvprintw(y, 4, "%s", pathbuf);
        curs_set(0);
    }

    if (is_selected) attroff(A_REVERSE);
    y += 2;

    is_selected = (st->settings_selected == 1);
    if (is_selected) attron(A_REVERSE);
    mvprintw(y, 2, "Seek Step (seconds): %d", st->config.seek_step);
    if (is_selected) attroff(A_REVERSE);
    y += 2;

    is_selected = (st->settings_selected == 2);
    if (is_selected) attron(A_REVERSE);
    mvprintw(y, 2, "Remember Session: %s", st->config.remember_session ? "ON" : "OFF");
    if (is_selected) attroff(A_REVERSE);
    y += 2;

    is_selected = (st->settings_selected == 3);
    if (is_selected) attron(A_REVERSE);
    mvprintw(y, 2, "Shuffle Mode: %s", st->config.shuffle_mode ? "ON" : "OFF");
    if (is_selected) attroff(A_REVERSE);
    y += 2;

    mvprintw(y, 2, "Up/Down: navigate | Enter: edit/toggle | Esc: back");
    y++;
    if (st->settings_editing) mvprintw(y, 2, "Editing: Enter to save, Esc to cancel");
}

static void draw_exit_dialog(int pending_count, int rows, int cols) {
    int dialog_w = 50, dialog_h = 8;
    int start_x  = (cols - dialog_w) / 2;
    int start_y  = (rows - dialog_h) / 2;

    for (int y = start_y; y < start_y + dialog_h; y++) mvhline(y, start_x, ' ', dialog_w);

    attron(A_REVERSE);
    mvprintw(start_y, start_x, "%-*s", dialog_w, "");
    mvprintw(start_y, start_x + (dialog_w - 16) / 2, " Download Queue ");
    attroff(A_REVERSE);

    mvprintw(start_y + 2, start_x + 2, "Downloads in progress: %d remaining", pending_count);
    mvprintw(start_y + 4, start_x + 2, "Downloads will resume on next startup.");

    attron(A_BOLD);
    mvprintw(start_y + 6, start_x + 2, "[q] Quit anyway    [Esc] Cancel");
    attroff(A_BOLD);

    refresh();
}

static void draw_about_view(int rows, int cols) {
    int dialog_w = 60, dialog_h = 16;
    int start_x  = (cols - dialog_w) / 2;
    int start_y  = (rows - dialog_h) / 2;

    attron(A_BOLD);
    for (int y = start_y; y < start_y + dialog_h; y++) mvhline(y, start_x, ' ', dialog_w);
    attroff(A_BOLD);

    attron(A_BOLD);
    mvaddch(start_y,              start_x,              ACS_ULCORNER);
    mvaddch(start_y,              start_x + dialog_w-1, ACS_URCORNER);
    mvaddch(start_y + dialog_h-1, start_x,              ACS_LLCORNER);
    mvaddch(start_y + dialog_h-1, start_x + dialog_w-1, ACS_LRCORNER);
    mvhline(start_y,              start_x + 1, ACS_HLINE, dialog_w - 2);
    mvhline(start_y + dialog_h-1, start_x + 1, ACS_HLINE, dialog_w - 2);
    mvvline(start_y + 1, start_x,              ACS_VLINE, dialog_h - 2);
    mvvline(start_y + 1, start_x + dialog_w-1, ACS_VLINE, dialog_h - 2);
    attroff(A_BOLD);

    attron(A_BOLD | A_REVERSE);
    mvprintw(start_y + 2, start_x + (dialog_w - 15) / 2, " ShellBeats v0.6");
    attroff(A_BOLD | A_REVERSE);

    mvprintw(start_y + 4, start_x + (dialog_w - 28) / 2, "made by Lalo for Nami & Elia");
    mvprintw(start_y + 6, start_x + (dialog_w - 44) / 2, "A terminal-based music player for YouTube");

    mvprintw(start_y + 8,  start_x + 4, "Features:");
    mvprintw(start_y + 9,  start_x + 6, "* Search and stream music from YouTube");
    mvprintw(start_y + 10, start_x + 6, "* Download songs as MP3");
    mvprintw(start_y + 11, start_x + 6, "* Create and manage playlists");
    mvprintw(start_y + 12, start_x + 6, "* Offline playback from local files");

    attron(A_DIM);
    mvprintw(start_y + 14, start_x + (dialog_w - 40) / 2, "Built with mpv, yt-dlp, and ncurses");
    attroff(A_DIM);

    refresh();
}

// ============================================================================
// Public API
// ============================================================================

void draw_ui(AppState *st, const char *status) {
    erase();
    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    draw_header(cols, st->view);

    switch (st->view) {
        case VIEW_SEARCH:
            draw_search_view(st, status, rows, cols);
            break;
        case VIEW_PLAYLISTS:
            draw_playlists_view(st, status, rows, cols);
            break;
        case VIEW_PLAYLIST_SONGS:
            draw_playlist_songs_view(st, status, rows, cols);
            break;
        case VIEW_ADD_TO_PLAYLIST:
            draw_add_to_playlist_view(st, status, rows, cols);
            break;
        case VIEW_SETTINGS:
            draw_settings_view(st, status, rows, cols);
            break;
        case VIEW_ABOUT:
            draw_about_view(rows, cols);
            break;
    }

    draw_now_playing(st, rows, cols);
    refresh();
}

int get_string_input(char *buf, size_t bufsz, const char *prompt) {
    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    int y = rows - 1;
    move(y, 0);
    clrtoeol();

    attron(A_BOLD);
    mvprintw(y, 0, "%s", prompt);
    attroff(A_BOLD);
    refresh();

    int prompt_len = (int)strlen(prompt);
    int max_input  = cols - prompt_len - 2;
    if (max_input > (int)bufsz - 1) max_input = (int)bufsz - 1;
    if (max_input < 1) max_input = 1;

    timeout(-1);
    echo();
    curs_set(1);
    move(y, prompt_len);
    getnstr(buf, max_input);
    noecho();
    curs_set(0);
    timeout(100);

    char *trimmed = trim_whitespace(buf);
    if (trimmed != buf) memmove(buf, trimmed, strlen(trimmed) + 1);

    return (int)strlen(buf);
}

void show_help(void) {
    erase();
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    (void)cols;

    int y = 2;
    attron(A_BOLD);
    mvprintw(y++, 2, "ShellBeats v0.6 | Help");
    attroff(A_BOLD);
    y++;

    mvprintw(y++, 4, "PLAYBACK:");
    mvprintw(y++, 6, "/,s         Search YouTube");
    mvprintw(y++, 6, "Enter       Play selected");
    mvprintw(y++, 6, "Space       Pause/Resume");
    mvprintw(y++, 6, "n/p         Next/Previous track");
    mvprintw(y++, 6, "x           Stop playback");
    mvprintw(y++, 6, "R           Toggle shuffle mode");
    mvprintw(y++, 6, "Left/Right  Seek backward/forward");
    mvprintw(y++, 6, "t           Jump to time (mm:ss)");
    y++;

    mvprintw(y++, 4, "NAVIGATION:");
    mvprintw(y++, 6, "Up/Down/j/k Navigate list");
    mvprintw(y++, 6, "PgUp/PgDn   Page up/down");
    mvprintw(y++, 6, "g/G         Go to start/end");
    mvprintw(y++, 6, "Esc         Go back");
    y++;

    mvprintw(y++, 4, "PLAYLISTS:");
    mvprintw(y++, 6, "f           Open playlists menu");
    mvprintw(y++, 6, "a           Add song to playlist");
    mvprintw(y++, 6, "c           Create new playlist");
    mvprintw(y++, 6, "e           Rename playlist");
    mvprintw(y++, 6, "r           Remove song from playlist");
    mvprintw(y++, 6, "d/D         Download song / Download all");
    mvprintw(y++, 6, "p           Import YouTube playlist");
    mvprintw(y++, 6, "u           Sync YouTube playlist");
    mvprintw(y++, 6, "x           Delete playlist");
    y++;

    mvprintw(y++, 4, "OTHER:");
    mvprintw(y++, 6, "S           Settings");
    mvprintw(y++, 6, "i           About");
    mvprintw(y++, 6, "h,?         Show this help");
    mvprintw(y++, 6, "q           Quit");

    attron(A_REVERSE);
    mvprintw(rows - 2, 2, " Press any key to continue... ");
    attroff(A_REVERSE);

    refresh();
    timeout(-1);
    getch();
    timeout(100);
}

bool check_dependencies(AppState *st, char *errmsg, size_t errsz) {
    bool ytdlp_found = false;
    if (st->ytdlp_has_local && file_exists(st->ytdlp_local_path)) {
        ytdlp_found = true;
    } else {
        FILE *fp = popen("which yt-dlp 2>/dev/null", "r");
        if (fp) {
            char buf[256];
            ytdlp_found = (fgets(buf, sizeof(buf), fp) != NULL && buf[0] == '/');
            pclose(fp);
        }
    }
    if (!ytdlp_found && !st->ytdlp_updating) {
        snprintf(errmsg, errsz, "yt-dlp not found! Will be downloaded automatically on next start.");
        return false;
    }

    FILE *mpv_fp = popen("which mpv 2>/dev/null", "r");
    if (mpv_fp) {
        char buf[256];
        bool found = (fgets(buf, sizeof(buf), mpv_fp) != NULL && buf[0] == '/');
        pclose(mpv_fp);
        if (!found) {
            snprintf(errmsg, errsz, "mpv not found! Install with: apt install mpv");
            return false;
        }
    }
    return true;
}

// Public helper for exit dialog (called from main event loop)
void draw_exit_dialog_pub(AppState *st, int pending_count) {
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    draw_ui(st, "");
    draw_exit_dialog(pending_count, rows, cols);
}
