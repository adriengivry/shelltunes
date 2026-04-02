#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "search.h"
#include "config.h"
#include "log.h"
#include "utils.h"

void free_search_results(AppState *st) {
    for (int i = 0; i < st->search_count; i++) {
        free(st->search_results[i].title);
        free(st->search_results[i].video_id);
        free(st->search_results[i].url);
        st->search_results[i].title    = NULL;
        st->search_results[i].video_id = NULL;
        st->search_results[i].url      = NULL;
    }
    st->search_count    = 0;
    st->search_selected = 0;
    st->search_scroll   = 0;
}

int run_search(AppState *st, const char *raw_query) {
    free_search_results(st);

    char query_buf[256];
    snprintf(query_buf, sizeof(query_buf), "%s", raw_query);
    char *query = trim_whitespace(query_buf);
    if (!query[0]) return 0;

    sb_log("[PLAYBACK] run_search: query=\"%s\"", query);

    // Escape for shell
    char escaped[512];
    size_t j = 0;
    for (size_t i = 0; query[i] && j < sizeof(escaped) - 5; i++) {
        char c = query[i];
        if (c == '"' || c == '\\' || c == '$' || c == '`')
            escaped[j++] = '\\';
        escaped[j++] = c;
    }
    escaped[j] = '\0';

    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
             "%s --flat-playlist --quiet --no-warnings "
             "--print '%%(title)s|||%%(id)s|||%%(duration)s' "
             "\"ytsearch%d:%s\" 2>/dev/null",
             get_ytdlp_cmd(st), MAX_RESULTS, escaped);

    sb_log("[PLAYBACK] run_search: executing: %s", cmd);

    FILE *fp = popen(cmd, "r");
    if (!fp) { sb_log("[PLAYBACK] run_search: popen failed"); return -1; }

    char *line = NULL;
    size_t cap = 0;
    int count  = 0;

    while (count < MAX_RESULTS && getline(&line, &cap, fp) != -1) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';

        if (!line[0]) continue;
        if (strncmp(line, "ERROR",   5) == 0) continue;
        if (strncmp(line, "WARNING", 7) == 0) continue;

        char *sep1 = strstr(line, "|||");
        if (!sep1) continue;
        *sep1 = '\0';
        char *sep2 = strstr(sep1 + 3, "|||");
        if (!sep2) continue;
        *sep2 = '\0';

        const char *title       = line;
        const char *video_id    = sep1 + 3;
        const char *duration_str = sep2 + 3;
        if (!video_id[0]) continue;

        size_t id_len = strlen(video_id);
        if (id_len < 5 || id_len > 20) continue;

        st->search_results[count].title    = strdup(title);
        st->search_results[count].video_id = strdup(video_id);
        st->search_results[count].duration = atoi(duration_str);

        char url[256];
        snprintf(url, sizeof(url), "https://www.youtube.com/watch?v=%s", video_id);
        st->search_results[count].url = strdup(url);

        if (st->search_results[count].title &&
            st->search_results[count].video_id &&
            st->search_results[count].url) {
            count++;
        } else {
            free(st->search_results[count].title);
            free(st->search_results[count].video_id);
            free(st->search_results[count].url);
        }
    }
    free(line);
    pclose(fp);

    st->search_count    = count;
    st->search_selected = 0;
    st->search_scroll   = 0;
    snprintf(st->query, sizeof(st->query), "%s", query);

    sb_log("[PLAYBACK] run_search: found %d results for \"%s\"", count, query);
    return count;
}
