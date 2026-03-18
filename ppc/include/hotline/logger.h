/*
 * logger.h - Logging interface (vtable)
 *
 * Maps to: hotline/logger.go (Logger interface)
 *
 * Simple variadic-style logging. Implementations can write to
 * stderr, a file with rotation, syslog, etc.
 */

#ifndef HOTLINE_LOGGER_H
#define HOTLINE_LOGGER_H

typedef struct hl_logger hl_logger_t;

/* Logger vtable — maps to Go Logger interface */
typedef struct {
    void (*debug)(hl_logger_t *self, const char *fmt, ...);
    void (*info)(hl_logger_t *self, const char *fmt, ...);
    void (*error)(hl_logger_t *self, const char *fmt, ...);
    void (*free)(hl_logger_t *self);
} hl_logger_vtable_t;

/* Logger base struct — concrete implementations embed this */
struct hl_logger {
    const hl_logger_vtable_t *vt;
};

/* Convenience macros */
#define hl_log_debug(logger, ...) ((logger)->vt->debug((logger), __VA_ARGS__))
#define hl_log_info(logger, ...)  ((logger)->vt->info((logger), __VA_ARGS__))
#define hl_log_error(logger, ...) ((logger)->vt->error((logger), __VA_ARGS__))

/*
 * hl_stderr_logger_new - Create a simple logger that writes to stderr.
 * Suitable for development/testing. Returns NULL on failure.
 */
hl_logger_t *hl_stderr_logger_new(void);

#endif /* HOTLINE_LOGGER_H */
