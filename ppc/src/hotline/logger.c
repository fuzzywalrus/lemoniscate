/*
 * logger.c - Simple stderr logger implementation
 *
 * Maps to: Go slog.Logger (simplified)
 */

#include "hotline/logger.h"
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <time.h>

typedef struct {
    hl_logger_t base;
} stderr_logger_t;

static void log_write(const char *level, const char *fmt, va_list ap)
{
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    char timebuf[32];
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", tm);

    fprintf(stderr, "%s [%s] ", timebuf, level);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
}

static void stderr_debug(hl_logger_t *self, const char *fmt, ...)
{
    (void)self;
    va_list ap;
    va_start(ap, fmt);
    log_write("DEBUG", fmt, ap);
    va_end(ap);
}

static void stderr_info(hl_logger_t *self, const char *fmt, ...)
{
    (void)self;
    va_list ap;
    va_start(ap, fmt);
    log_write("INFO", fmt, ap);
    va_end(ap);
}

static void stderr_error(hl_logger_t *self, const char *fmt, ...)
{
    (void)self;
    va_list ap;
    va_start(ap, fmt);
    log_write("ERROR", fmt, ap);
    va_end(ap);
}

static void stderr_free(hl_logger_t *self)
{
    free(self);
}

static const hl_logger_vtable_t stderr_vtable = {
    .debug = stderr_debug,
    .info  = stderr_info,
    .error = stderr_error,
    .free  = stderr_free
};

hl_logger_t *hl_stderr_logger_new(void)
{
    stderr_logger_t *l = (stderr_logger_t *)calloc(1, sizeof(stderr_logger_t));
    if (!l) return NULL;
    l->base.vt = &stderr_vtable;
    return &l->base;
}
