/*
 * logger_impl.h - Production logger with file rotation
 *
 * Multiplexes output to stderr + rotating log file.
 * Rotation: 100MB max per file, 3 backups, 365 day retention.
 */

#ifndef MOBIUS_LOGGER_IMPL_H
#define MOBIUS_LOGGER_IMPL_H

#include "hotline/logger.h"

#define MOBIUS_LOG_MAX_SIZE    (100 * 1024 * 1024)  /* 100 MB */
#define MOBIUS_LOG_MAX_BACKUPS 3
#define MOBIUS_LOG_MAX_AGE     365  /* days */

/*
 * mobius_file_logger_new - Create a logger that writes to stderr + rotating file.
 *
 * log_file: path to log file (NULL for stderr-only)
 * log_level: "debug", "info", or "error"
 */
hl_logger_t *mobius_file_logger_new(const char *log_file, const char *log_level);

#endif /* MOBIUS_LOGGER_IMPL_H */
