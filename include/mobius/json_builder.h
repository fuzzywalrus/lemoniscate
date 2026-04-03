/*
 * json_builder.h - JSON string escaping and dynamic buffer for building payloads
 *
 * Used by Mnemosyne sync to build JSON POSTs without a JSON library.
 */

#ifndef MOBIUS_JSON_BUILDER_H
#define MOBIUS_JSON_BUILDER_H

#include <stddef.h>
#include <stdint.h>

/* --- Dynamic buffer for building JSON payloads --- */

typedef struct {
    char   *data;
    size_t  len;
    size_t  cap;
} json_buf_t;

/* Initialize a buffer (call before use, or zero-init is fine) */
void json_buf_init(json_buf_t *buf);

/* Free the buffer's memory */
void json_buf_free(json_buf_t *buf);

/* Append raw bytes */
void json_buf_append(json_buf_t *buf, const char *str, size_t len);

/* Append a null-terminated string */
void json_buf_append_str(json_buf_t *buf, const char *str);

/* Append formatted output (printf-style) */
void json_buf_printf(json_buf_t *buf, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

/* Append a JSON-escaped string (without surrounding quotes) */
void json_buf_append_escaped(json_buf_t *buf, const char *str);

/* Convenience: append "key": "escaped_value" */
void json_buf_add_string(json_buf_t *buf, const char *key, const char *value);

/* Convenience: append "key": number */
void json_buf_add_int(json_buf_t *buf, const char *key, int value);

/* Convenience: append "key": true/false */
void json_buf_add_bool(json_buf_t *buf, const char *key, int value);

/* --- Standalone JSON string escaping --- */

/*
 * json_escape_string - Escape a string for JSON embedding.
 *
 * Handles: \" \\ \n \r \t and control characters (\\u00XX).
 * Writes into out_buf (up to out_size bytes including null terminator).
 * Returns the number of characters written (excluding null), or -1 if
 * the buffer was too small (output is still null-terminated but truncated).
 */
int json_escape_string(const char *input, char *out_buf, size_t out_size);

#endif /* MOBIUS_JSON_BUILDER_H */
