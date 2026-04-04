/*
 * jsonl_message_board.c - JSONL-based message board storage
 *
 * Janus-compatible format: one JSON object per line in MessageBoard.jsonl.
 * Provides the same mobius_flat_news_t interface for transparent backend swap.
 */

#include "mobius/jsonl_message_board.h"
#include "mobius/json_builder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* --- Internal: parse a single JSONL line into an mb_post_t --- */

static int parse_jsonl_line(const char *line, mb_post_t *post)
{
    memset(post, 0, sizeof(*post));

    /* Minimal JSON parser for known fields */
    const char *p;

    /* id */
    p = strstr(line, "\"id\":");
    if (p) post->id = atoi(p + 5);

    /* nick */
    p = strstr(line, "\"nick\":\"");
    if (p) {
        p += 8;
        const char *end = strchr(p, '"');
        if (end) {
            size_t len = (size_t)(end - p);
            if (len >= sizeof(post->nick)) len = sizeof(post->nick) - 1;
            memcpy(post->nick, p, len);
        }
    }

    /* login */
    p = strstr(line, "\"login\":\"");
    if (p) {
        p += 9;
        const char *end = strchr(p, '"');
        if (end) {
            size_t len = (size_t)(end - p);
            if (len >= sizeof(post->login)) len = sizeof(post->login) - 1;
            memcpy(post->login, p, len);
        }
    }

    /* ts */
    p = strstr(line, "\"ts\":\"");
    if (p) {
        p += 6;
        const char *end = strchr(p, '"');
        if (end) {
            size_t len = (size_t)(end - p);
            if (len >= sizeof(post->ts)) len = sizeof(post->ts) - 1;
            memcpy(post->ts, p, len);
        }
    }

    /* body — may contain escaped characters */
    p = strstr(line, "\"body\":\"");
    if (p) {
        p += 8;
        /* Find closing quote, handling escaped quotes */
        json_buf_t buf;
        json_buf_init(&buf);
        while (*p && !(*p == '"' && *(p - 1) != '\\')) {
            if (*p == '\\' && *(p + 1)) {
                switch (*(p + 1)) {
                case '"': json_buf_append(&buf, "\"", 1); p += 2; break;
                case '\\': json_buf_append(&buf, "\\", 1); p += 2; break;
                case 'n': json_buf_append(&buf, "\n", 1); p += 2; break;
                case 'r': json_buf_append(&buf, "\r", 1); p += 2; break;
                case 't': json_buf_append(&buf, "\t", 1); p += 2; break;
                default: json_buf_append(&buf, p, 1); p++; break;
                }
            } else {
                json_buf_append(&buf, p, 1);
                p++;
            }
        }
        if (buf.data) {
            post->body = buf.data; /* transfer ownership */
            buf.data = NULL;
        }
        json_buf_free(&buf);
    }

    if (!post->body) {
        post->body = strdup("");
    }

    return (post->id > 0) ? 0 : -1;
}

/* --- Load all posts from JSONL file --- */

static int load_posts(const char *path, mb_post_t **out, int *out_count,
                      int *out_max_id)
{
    *out = NULL;
    *out_count = 0;
    *out_max_id = 0;

    FILE *f = fopen(path, "r");
    if (!f) return 0; /* empty board is fine */

    int cap = 64;
    mb_post_t *posts = (mb_post_t *)calloc((size_t)cap, sizeof(mb_post_t));
    if (!posts) { fclose(f); return -1; }

    char line[16384];
    int count = 0;
    int max_id = 0;

    while (fgets(line, sizeof(line), f)) {
        /* Strip newline */
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = '\0';
        if (len == 0) continue;

        if (count >= cap) {
            cap *= 2;
            mb_post_t *tmp = (mb_post_t *)realloc(posts, (size_t)cap * sizeof(mb_post_t));
            if (!tmp) break;
            posts = tmp;
        }

        if (parse_jsonl_line(line, &posts[count]) == 0) {
            if (posts[count].id > max_id) max_id = posts[count].id;
            count++;
        }
    }
    fclose(f);

    *out = posts;
    *out_count = count;
    *out_max_id = max_id;
    return 0;
}

/* --- Build flat text representation for Hotline wire protocol --- */

static char *build_flat_text(mb_post_t *posts, int count, size_t *out_len)
{
    json_buf_t buf;
    json_buf_init(&buf);

    /* Newest first (posts are stored oldest first in JSONL, reverse for display) */
    int i;
    for (i = count - 1; i >= 0; i--) {
        if (i < count - 1) {
            json_buf_append_str(&buf, "\r_____________________________________________\r");
        }

        /* Format: "nick (date):\r\rbody" */
        char date_display[32] = "";
        if (posts[i].ts[0]) {
            /* Extract date portion from ISO timestamp */
            strncpy(date_display, posts[i].ts, 10);
            date_display[10] = '\0';
        }

        json_buf_printf(&buf, "%s (%s)\r\r",
                        posts[i].nick[0] ? posts[i].nick : "unknown",
                        date_display[0] ? date_display : "unknown date");
        if (posts[i].body) {
            json_buf_append_str(&buf, posts[i].body);
        }
    }

    *out_len = buf.len;
    return buf.data;
}

/* --- JSONL-backed flat_news_t --- */

/*
 * We reuse mobius_flat_news_t but store JSONL internally.
 * The `data` field holds the formatted text for Hotline display.
 * The `file_path` points to the .jsonl file.
 * We tag JSONL instances by checking if file_path ends with ".jsonl".
 */

static int is_jsonl_backend(mobius_flat_news_t *fn)
{
    size_t len = strlen(fn->file_path);
    return (len > 6 && strcmp(fn->file_path + len - 6, ".jsonl") == 0);
}

mobius_flat_news_t *mobius_jsonl_news_new(const char *path)
{
    mobius_flat_news_t *fn = (mobius_flat_news_t *)calloc(1, sizeof(*fn));
    if (!fn) return NULL;

    strncpy(fn->file_path, path, sizeof(fn->file_path) - 1);
    pthread_mutex_init(&fn->mu, NULL);

    /* Load posts and build display text */
    mb_post_t *posts = NULL;
    int count = 0, max_id = 0;
    load_posts(path, &posts, &count, &max_id);

    fn->data = build_flat_text(posts, count, &fn->data_len);
    mobius_jsonl_free_posts(posts, count);

    return fn;
}

int mobius_jsonl_post_count(mobius_flat_news_t *fn)
{
    if (!fn || !is_jsonl_backend(fn)) return 0;

    pthread_mutex_lock(&fn->mu);

    mb_post_t *posts = NULL;
    int count = 0, max_id = 0;
    load_posts(fn->file_path, &posts, &count, &max_id);
    mobius_jsonl_free_posts(posts, count);

    pthread_mutex_unlock(&fn->mu);
    return count;
}

int mobius_jsonl_get_posts(mobius_flat_news_t *fn,
                           mb_post_t **out_posts, int *out_count)
{
    if (!fn || !is_jsonl_backend(fn)) {
        *out_posts = NULL;
        *out_count = 0;
        return -1;
    }

    pthread_mutex_lock(&fn->mu);
    int max_id = 0;
    int rc = load_posts(fn->file_path, out_posts, out_count, &max_id);
    pthread_mutex_unlock(&fn->mu);
    return rc;
}

void mobius_jsonl_free_posts(mb_post_t *posts, int count)
{
    if (!posts) return;
    int i;
    for (i = 0; i < count; i++) {
        free(posts[i].body);
    }
    free(posts);
}

/* --- JSONL-aware prepend (called from handler via flat_news interface) --- */

/*
 * mobius_jsonl_prepend - Intercept a flat_news prepend for JSONL backends.
 *
 * The handler sends formatted text: "...nick (date)\r\rbody".
 * We extract the nick and body, create a structured JSONL entry,
 * and append it to the file. Then rebuild the display text.
 *
 * Called from the transaction handler which already has the flat_news_t*.
 * This function is called by flat_news_prepend when the backend is JSONL.
 */
int mobius_jsonl_prepend(mobius_flat_news_t *fn, const char *data, size_t len)
{
    if (!fn || !is_jsonl_backend(fn)) return -1;

    /* Parse the formatted post to extract nick and body.
     * Format: "\r___...\rnick (date)\r\rbody" */
    char nick[128] = "unknown";
    const char *body = data;
    size_t body_len = len;

    /* Skip leading separator if present */
    const char *p = data;
    const char *end = data + len;
    const char *sep = "\r_____________________________________________\r";
    size_t sep_len = strlen(sep);
    if (len >= sep_len && memcmp(p, sep, sep_len) == 0) {
        p += sep_len;
    }

    /* Extract nick from "nick (date)\r\r" */
    const char *cr = memchr(p, '\r', (size_t)(end - p));
    if (cr && cr > p) {
        char header[512];
        size_t hlen = (size_t)(cr - p);
        if (hlen >= sizeof(header)) hlen = sizeof(header) - 1;
        memcpy(header, p, hlen);
        header[hlen] = '\0';

        char *paren = strrchr(header, '(');
        if (paren && paren > header) {
            size_t nlen = (size_t)(paren - header);
            while (nlen > 0 && header[nlen-1] == ' ') nlen--;
            if (nlen > 0 && nlen < sizeof(nick)) {
                memcpy(nick, header, nlen);
                nick[nlen] = '\0';
            }
        }

        /* Body is after the header + \r\r */
        const char *bs = cr;
        while (bs < end && *bs == '\r') bs++;
        body = bs;
        body_len = (size_t)(end - bs);
    }

    /* Get next ID */
    mb_post_t *posts = NULL;
    int count = 0, max_id = 0;
    load_posts(fn->file_path, &posts, &count, &max_id);
    mobius_jsonl_free_posts(posts, count);

    int new_id = max_id + 1;

    /* Generate timestamp */
    char ts[64];
    time_t now = time(NULL);
    struct tm *tm = gmtime(&now);
    snprintf(ts, sizeof(ts), "%04d-%02d-%02dT%02d:%02d:%02dZ",
             tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
             tm->tm_hour, tm->tm_min, tm->tm_sec);

    /* Build JSONL line */
    json_buf_t buf;
    json_buf_init(&buf);
    json_buf_append_str(&buf, "{");
    json_buf_add_int(&buf, "id", new_id);
    json_buf_append_str(&buf, ", ");
    json_buf_add_string(&buf, "login", "");
    json_buf_append_str(&buf, ", ");
    json_buf_add_string(&buf, "nick", nick);
    json_buf_append_str(&buf, ", ");
    json_buf_append_str(&buf, "\"body\": \"");
    /* Escape body content */
    if (body_len > 0) {
        size_t i;
        for (i = 0; i < body_len; i++) {
            unsigned char c = (unsigned char)body[i];
            switch (c) {
            case '"':  json_buf_append(&buf, "\\\"", 2); break;
            case '\\': json_buf_append(&buf, "\\\\", 2); break;
            case '\n': json_buf_append(&buf, "\\n", 2); break;
            case '\r': json_buf_append(&buf, "\\r", 2); break;
            case '\t': json_buf_append(&buf, "\\t", 2); break;
            default:
                if (c < 0x20) {
                    char esc[8]; snprintf(esc, sizeof(esc), "\\u%04x", c);
                    json_buf_append(&buf, esc, 6);
                } else {
                    json_buf_append(&buf, (const char *)&c, 1);
                }
            }
        }
    }
    json_buf_append_str(&buf, "\"");
    json_buf_append_str(&buf, ", ");
    json_buf_add_string(&buf, "ts", ts);
    json_buf_append_str(&buf, "}\n");

    /* Append to file */
    FILE *f = fopen(fn->file_path, "a");
    if (f && buf.data) {
        fwrite(buf.data, 1, buf.len, f);
        fclose(f);
    }
    json_buf_free(&buf);

    /* Rebuild display text */
    posts = NULL;
    count = 0;
    max_id = 0;
    load_posts(fn->file_path, &posts, &count, &max_id);

    pthread_mutex_lock(&fn->mu);
    free(fn->data);
    fn->data = build_flat_text(posts, count, &fn->data_len);
    pthread_mutex_unlock(&fn->mu);

    mobius_jsonl_free_posts(posts, count);
    return 0;
}

/* --- Migration from flat text --- */

int mobius_migrate_flat_to_jsonl(const char *txt_path, const char *jsonl_path)
{
    FILE *f = fopen(txt_path, "rb");
    if (!f) return -1;

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (len <= 0) {
        fclose(f);
        /* Create empty JSONL file */
        FILE *out = fopen(jsonl_path, "w");
        if (out) fclose(out);
        return 0;
    }

    char *text = (char *)malloc((size_t)len + 1);
    if (!text) { fclose(f); return -1; }
    fread(text, 1, (size_t)len, f);
    text[len] = '\0';
    fclose(f);

    /* Split on delimiter: "\r_____________________________________________\r" */
    FILE *out = fopen(jsonl_path, "w");
    if (!out) { free(text); return -1; }

    int post_id = 1;
    char *p = text;

    /* Posts are stored newest-first in the flat file.
     * Split by the separator line and parse each section. */
    const char *sep = "\r_____________________________________________\r";
    size_t sep_len = strlen(sep);

    while (p && *p) {
        /* Find next separator */
        char *next = strstr(p, sep);
        size_t section_len = next ? (size_t)(next - p) : strlen(p);

        if (section_len > 0) {
            /* Extract nick from "nick (date)\r\r" header */
            char nick[128] = "unknown";
            char date_str[64] = "";

            /* Find first \r to get the header line */
            char *cr = memchr(p, '\r', section_len);
            if (cr && cr > p) {
                size_t hdr_len = (size_t)(cr - p);
                char header[512];
                if (hdr_len >= sizeof(header)) hdr_len = sizeof(header) - 1;
                memcpy(header, p, hdr_len);
                header[hdr_len] = '\0';

                /* Try to parse "nick (date)" */
                char *paren = strrchr(header, '(');
                if (paren && paren > header) {
                    size_t nick_len = (size_t)(paren - header);
                    while (nick_len > 0 && header[nick_len - 1] == ' ')
                        nick_len--;
                    if (nick_len > 0 && nick_len < sizeof(nick)) {
                        memcpy(nick, header, nick_len);
                        nick[nick_len] = '\0';
                    }
                    /* Extract date from parens */
                    char *close = strchr(paren, ')');
                    if (close) {
                        size_t dlen = (size_t)(close - paren - 1);
                        if (dlen < sizeof(date_str)) {
                            memcpy(date_str, paren + 1, dlen);
                            date_str[dlen] = '\0';
                        }
                    }
                }
            }

            /* Body is everything after the header line(s) */
            const char *body = p;
            size_t body_len = section_len;
            if (cr) {
                /* Skip header + any \r separators */
                const char *body_start = cr;
                while (*body_start == '\r' && (size_t)(body_start - p) < section_len)
                    body_start++;
                body = body_start;
                body_len = section_len - (size_t)(body_start - p);
            }

            /* Generate ISO timestamp */
            char ts[64];
            time_t now = time(NULL);
            struct tm *tm = gmtime(&now);
            snprintf(ts, sizeof(ts), "%04d-%02d-%02dT%02d:%02d:%02dZ",
                     tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
                     tm->tm_hour, tm->tm_min, tm->tm_sec);

            /* Write JSONL line */
            json_buf_t buf;
            json_buf_init(&buf);
            json_buf_append_str(&buf, "{");
            json_buf_add_int(&buf, "id", post_id);
            json_buf_append_str(&buf, ", ");
            json_buf_add_string(&buf, "login", "");
            json_buf_append_str(&buf, ", ");
            json_buf_add_string(&buf, "nick", nick);
            json_buf_append_str(&buf, ", ");

            /* Escape body for JSON */
            json_buf_append_str(&buf, "\"body\": \"");
            json_buf_append(&buf, body, body_len > 0 ? body_len : 0);
            json_buf_append_str(&buf, "\"");

            json_buf_append_str(&buf, ", ");
            json_buf_add_string(&buf, "ts", ts);
            json_buf_append_str(&buf, "}\n");

            if (buf.data)
                fwrite(buf.data, 1, buf.len, out);
            json_buf_free(&buf);

            post_id++;
        }

        if (next) {
            p = next + sep_len;
        } else {
            break;
        }
    }

    fclose(out);
    free(text);

    /* Rename old file to .legacy */
    char legacy[1100];
    snprintf(legacy, sizeof(legacy), "%s.legacy", txt_path);
    rename(txt_path, legacy);

    return 0;
}
