#define _GNU_SOURCE

#include <stdarg.h>
#include <stdio.h>
#include <time.h>

#include "log.h"

static FILE *g_log_file = NULL;

void log_open(const char *path) {
    g_log_file = fopen(path, "a");
}

void log_close(void) {
    if (g_log_file) {
        fclose(g_log_file);
        g_log_file = NULL;
    }
}

void sb_log(const char *fmt, ...) {
    if (!g_log_file) return;

    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    fprintf(g_log_file, "[%04d-%02d-%02d %02d:%02d:%02d] ",
            tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
            tm->tm_hour, tm->tm_min, tm->tm_sec);

    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log_file, fmt, ap);
    va_end(ap);

    fputc('\n', g_log_file);
    fflush(g_log_file);
}
