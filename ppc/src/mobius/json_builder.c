/*
 * json_builder.c - JSON string escaping and dynamic buffer
 */

#include "mobius/json_builder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/* --- Dynamic buffer --- */

#define JSON_BUF_INITIAL_CAP 256

void json_buf_init(json_buf_t *buf)
{
    buf->data = NULL;
    buf->len = 0;
    buf->cap = 0;
}

void json_buf_free(json_buf_t *buf)
{
    free(buf->data);
    buf->data = NULL;
    buf->len = 0;
    buf->cap = 0;
}

static void json_buf_grow(json_buf_t *buf, size_t needed)
{
    if (buf->len + needed <= buf->cap)
        return;

    size_t new_cap = buf->cap ? buf->cap * 2 : JSON_BUF_INITIAL_CAP;
    while (new_cap < buf->len + needed)
        new_cap *= 2;

    char *new_data = (char *)realloc(buf->data, new_cap);
    if (!new_data)
        return; /* OOM — silent fail, caller checks buf->data */
    buf->data = new_data;
    buf->cap = new_cap;
}

void json_buf_append(json_buf_t *buf, const char *str, size_t len)
{
    if (len == 0) return;
    json_buf_grow(buf, len + 1);
    memcpy(buf->data + buf->len, str, len);
    buf->len += len;
    buf->data[buf->len] = '\0';
}

void json_buf_append_str(json_buf_t *buf, const char *str)
{
    json_buf_append(buf, str, strlen(str));
}

void json_buf_printf(json_buf_t *buf, const char *fmt, ...)
{
    va_list ap;

    /* First pass: measure needed size */
    va_start(ap, fmt);
    int needed = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);

    if (needed <= 0) return;

    json_buf_grow(buf, (size_t)needed + 1);

    va_start(ap, fmt);
    vsnprintf(buf->data + buf->len, (size_t)needed + 1, fmt, ap);
    va_end(ap);

    buf->len += (size_t)needed;
}

/* --- JSON string escaping --- */

static void escape_char(json_buf_t *buf, unsigned char c)
{
    switch (c) {
    case '"':  json_buf_append(buf, "\\\"", 2); break;
    case '\\': json_buf_append(buf, "\\\\", 2); break;
    case '\n': json_buf_append(buf, "\\n", 2);  break;
    case '\r': json_buf_append(buf, "\\r", 2);  break;
    case '\t': json_buf_append(buf, "\\t", 2);  break;
    default:
        if (c < 0x20) {
            /* Control character — \u00XX */
            char esc[8];
            snprintf(esc, sizeof(esc), "\\u%04x", c);
            json_buf_append(buf, esc, 6);
        } else {
            json_buf_append(buf, (const char *)&c, 1);
        }
        break;
    }
}

void json_buf_append_escaped(json_buf_t *buf, const char *str)
{
    if (!str) return;
    const unsigned char *p = (const unsigned char *)str;
    while (*p) {
        escape_char(buf, *p);
        p++;
    }
}

void json_buf_add_string(json_buf_t *buf, const char *key, const char *value)
{
    json_buf_append_str(buf, "\"");
    json_buf_append_str(buf, key);
    json_buf_append_str(buf, "\": \"");
    json_buf_append_escaped(buf, value);
    json_buf_append_str(buf, "\"");
}

void json_buf_add_int(json_buf_t *buf, const char *key, int value)
{
    json_buf_printf(buf, "\"%s\": %d", key, value);
}

void json_buf_add_bool(json_buf_t *buf, const char *key, int value)
{
    json_buf_printf(buf, "\"%s\": %s", key, value ? "true" : "false");
}

/* --- Standalone escaping into fixed buffer --- */

int json_escape_string(const char *input, char *out_buf, size_t out_size)
{
    if (!input || !out_buf || out_size == 0)
        return -1;

    /* Empty input is valid */
    if (input[0] == '\0') {
        out_buf[0] = '\0';
        return 0;
    }

    json_buf_t tmp;
    json_buf_init(&tmp);
    json_buf_append_escaped(&tmp, input);

    if (!tmp.data) {
        out_buf[0] = '\0';
        return -1;
    }

    size_t copy_len = tmp.len;
    int truncated = 0;
    if (copy_len >= out_size) {
        copy_len = out_size - 1;
        truncated = 1;
    }

    memcpy(out_buf, tmp.data, copy_len);
    out_buf[copy_len] = '\0';
    json_buf_free(&tmp);

    return truncated ? -1 : (int)copy_len;
}
