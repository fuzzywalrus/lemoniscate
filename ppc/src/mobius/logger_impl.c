/*
 * logger_impl.c - Production logger with file rotation
 *
 * Maps to: internal/mobius/logger.go (lumberjack equivalent)
 *
 * Simple log rotation: check file size before each write.
 * If over limit, rename current -> .1, shift .1 -> .2, etc.
 */

#include "mobius/logger_impl.h"
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>

typedef enum {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO  = 1,
    LOG_LEVEL_ERROR = 2
} log_level_t;

typedef struct {
    hl_logger_t base;
    FILE       *file;
    char        file_path[1024];
    log_level_t level;
} file_logger_t;

static log_level_t parse_level(const char *s)
{
    if (!s) return LOG_LEVEL_INFO;
    if (strcmp(s, "debug") == 0) return LOG_LEVEL_DEBUG;
    if (strcmp(s, "error") == 0) return LOG_LEVEL_ERROR;
    return LOG_LEVEL_INFO;
}

static void rotate_log(file_logger_t *fl)
{
    if (!fl->file || fl->file_path[0] == '\0') return;

    struct stat st;
    if (stat(fl->file_path, &st) != 0) return;
    if (st.st_size < MOBIUS_LOG_MAX_SIZE) return;

    fclose(fl->file);
    fl->file = NULL;

    /* Shift old backups: .3 -> delete, .2 -> .3, .1 -> .2, current -> .1 */
    char old_path[1040], new_path[1040];
    int i;
    for (i = MOBIUS_LOG_MAX_BACKUPS; i >= 1; i--) {
        snprintf(old_path, sizeof(old_path), "%s.%d", fl->file_path, i);
        if (i == MOBIUS_LOG_MAX_BACKUPS) {
            unlink(old_path);
        } else {
            snprintf(new_path, sizeof(new_path), "%s.%d", fl->file_path, i + 1);
            rename(old_path, new_path);
        }
    }

    snprintf(new_path, sizeof(new_path), "%s.1", fl->file_path);
    rename(fl->file_path, new_path);

    fl->file = fopen(fl->file_path, "a");
}

static void log_write(file_logger_t *fl, const char *level, const char *fmt, va_list ap)
{
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    char timebuf[32];
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", tm);

    /* Write to stderr */
    fprintf(stderr, "%s [%s] ", timebuf, level);
    va_list ap_copy;
    va_copy(ap_copy, ap);
    vfprintf(stderr, fmt, ap_copy);
    va_end(ap_copy);
    fprintf(stderr, "\n");

    /* Write to file (if configured) */
    if (fl->file) {
        rotate_log(fl);
        if (fl->file) {
            fprintf(fl->file, "%s [%s] ", timebuf, level);
            vfprintf(fl->file, fmt, ap);
            fprintf(fl->file, "\n");
            fflush(fl->file);
        }
    }
}

static void fl_debug(hl_logger_t *self, const char *fmt, ...)
{
    file_logger_t *fl = (file_logger_t *)self;
    if (fl->level > LOG_LEVEL_DEBUG) return;
    va_list ap;
    va_start(ap, fmt);
    log_write(fl, "DEBUG", fmt, ap);
    va_end(ap);
}

static void fl_info(hl_logger_t *self, const char *fmt, ...)
{
    file_logger_t *fl = (file_logger_t *)self;
    if (fl->level > LOG_LEVEL_INFO) return;
    va_list ap;
    va_start(ap, fmt);
    log_write(fl, "INFO", fmt, ap);
    va_end(ap);
}

static void fl_error(hl_logger_t *self, const char *fmt, ...)
{
    file_logger_t *fl = (file_logger_t *)self;
    va_list ap;
    va_start(ap, fmt);
    log_write(fl, "ERROR", fmt, ap);
    va_end(ap);
}

static void fl_free(hl_logger_t *self)
{
    file_logger_t *fl = (file_logger_t *)self;
    if (fl->file) fclose(fl->file);
    free(fl);
}

static const hl_logger_vtable_t file_logger_vtable = {
    .debug = fl_debug,
    .info  = fl_info,
    .error = fl_error,
    .free  = fl_free
};

hl_logger_t *mobius_file_logger_new(const char *log_file, const char *log_level)
{
    file_logger_t *fl = (file_logger_t *)calloc(1, sizeof(file_logger_t));
    if (!fl) return NULL;

    fl->base.vt = &file_logger_vtable;
    fl->level = parse_level(log_level);

    if (log_file && log_file[0] != '\0') {
        strncpy(fl->file_path, log_file, sizeof(fl->file_path) - 1);
        fl->file = fopen(log_file, "a");
        /* OK if file open fails — we still have stderr */
    }

    return &fl->base;
}
