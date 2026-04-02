#ifndef TYPES_H
#define TYPES_H

#include <pthread.h>
#include <stdbool.h>
#include <time.h>

#include "youtube_playlist.h"

// ============================================================================
// Constants
// ============================================================================

#define MAX_RESULTS        50
#define MAX_PLAYLISTS      50
#define MAX_PLAYLIST_ITEMS 500
#define MAX_DOWNLOAD_QUEUE 1000

#define IPC_SOCKET          "/tmp/shellbeats_mpv.sock"
#define CONFIG_DIR          ".shellbeats"
#define PLAYLISTS_DIR       "playlists"
#define PLAYLISTS_INDEX     "playlists.json"
#define CONFIG_FILE         "config.json"
#define DOWNLOAD_QUEUE_FILE "download_queue.json"
#define YTDLP_BIN_DIR       "bin"
#define YTDLP_BINARY        "yt-dlp"
#define YTDLP_VERSION_FILE  "yt-dlp.version"

// ============================================================================
// Types
// ============================================================================

typedef struct {
    char *name;
    char *filename;
    Song  items[MAX_PLAYLIST_ITEMS];
    int   count;
    bool  is_youtube_playlist;
} Playlist;

typedef struct {
    char download_path[1024];
    int  seek_step;
    bool remember_session;
    bool shuffle_mode;
} Config;

typedef enum {
    DOWNLOAD_PENDING,
    DOWNLOAD_ACTIVE,
    DOWNLOAD_COMPLETED,
    DOWNLOAD_FAILED
} DownloadStatus;

typedef struct {
    char          video_id[32];
    char          title[512];
    char          sanitized_filename[512];
    char          playlist_name[256];
    DownloadStatus status;
} DownloadTask;

typedef struct {
    DownloadTask    tasks[MAX_DOWNLOAD_QUEUE];
    int             count;
    int             completed;
    int             failed;
    int             current_idx;
    bool            active;
    pthread_mutex_t mutex;
    pthread_t       thread;
    bool            thread_running;
    bool            should_stop;
} DownloadQueue;

typedef enum {
    VIEW_SEARCH,
    VIEW_PLAYLISTS,
    VIEW_PLAYLIST_SONGS,
    VIEW_ADD_TO_PLAYLIST,
    VIEW_SETTINGS,
    VIEW_ABOUT
} ViewMode;

typedef struct {
    // Search
    Song search_results[MAX_RESULTS];
    int  search_count;
    int  search_selected;
    int  search_scroll;
    char query[256];

    // Playlists
    Playlist playlists[MAX_PLAYLISTS];
    int      playlist_count;
    int      playlist_selected;
    int      playlist_scroll;

    // Current playlist view
    int current_playlist_idx;
    int playlist_song_selected;
    int playlist_song_scroll;

    // Playback
    int    playing_index;
    bool   playing_from_playlist;
    int    playing_playlist_idx;
    bool   paused;
    time_t playback_started;

    // UI
    ViewMode view;
    int      add_to_playlist_selected;
    int      add_to_playlist_scroll;
    Song    *song_to_add;

    // Settings editing
    int  settings_selected;
    bool settings_editing;
    char settings_edit_buffer[1024];
    int  settings_edit_pos;

    // Config paths (config_dir is the base; all others are config_dir + short suffix)
    char config_dir[1024];
    char playlists_dir[2048];
    char playlists_index[2048];
    char config_file[2048];
    char download_queue_file[2048];
    char ytdlp_bin_dir[2048];
    char ytdlp_local_path[2048];
    char ytdlp_version_file[2048];

    // Configuration
    Config config;

    // Download queue
    DownloadQueue download_queue;

    // yt-dlp auto-update
    bool      ytdlp_updating;
    bool      ytdlp_update_done;
    bool      ytdlp_has_local;
    pthread_t ytdlp_update_thread;
    bool      ytdlp_update_thread_running;
    char      ytdlp_update_status[128];

    // Spinner
    int    spinner_frame;
    time_t last_spinner_update;

    // Session memory
    char last_query[256];
    Song cached_search[MAX_RESULTS];
    int  cached_search_count;
    int  last_playlist_idx;
    int  last_song_idx;
    bool was_playing_playlist;
} AppState;

#endif /* TYPES_H */
