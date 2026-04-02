#ifndef LOG_H
#define LOG_H

void log_open(const char *path);
void log_close(void);
void sb_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif /* LOG_H */
