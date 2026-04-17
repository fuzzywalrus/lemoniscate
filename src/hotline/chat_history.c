/*
 * chat_history.c - JSONL-based chat history storage
 *
 * Storage design: one append-only JSONL file per channel under
 * ChatHistory/channel-<N>.jsonl. Each line is a complete JSON object:
 *   {"id":1000,"ch":0,"ts":1729137536,"flags":0,"icon":128,
 *    "nick":"greg","body":"Hello everyone"}
 *
 * An in-memory index per channel maps message_id to (file_offset, length)
 * so cursor queries (BEFORE/AFTER/range) are O(log n) via binary search.
 *
 * Crash safety: on open, each channel file is scanned line-by-line
 * pulling only the "id":N substring. The offset after the last
 * successfully parsed line is recorded; trailing garbage from a power
 * loss or SIGKILL mid-append is truncated with ftruncate().
 *
 * Encryption at rest (optional): if a 32-byte key is provided, the
 * "body" field is replaced with "ENC:<base64(nonce||ciphertext||tag)>"
 * using ChaCha20-Poly1305. Metadata (id, ts, nick, flags) stays
 * plaintext so the startup scan, `tail -f`, and `grep` still work.
 *
 * Portability: PPC-safe. Uses ftello/fseeko with _FILE_OFFSET_BITS=64,
 * %llu formatting, fgets with fixed buffer (no getline). No pthreads.
 * Single-writer: the server is single-threaded.
 */

#define _FILE_OFFSET_BITS 64
#define _POSIX_C_SOURCE 200809L

#include "hotline/chat_history.h"
#include "hotline/chacha20poly1305.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define LM_CHAT_LINE_MAX        (LM_CHAT_MAX_NICK_LEN + LM_CHAT_MAX_BODY_LEN + 512)
#define LM_CHAT_MAX_CHANNELS    16
#define LM_CHAT_INDEX_GROW      256
#define LM_CHAT_NONCE_LEN       12
#define LM_CHAT_TAG_LEN         16
#define LM_CHAT_KEY_LEN         32
#define LM_CHAT_AAD             "chat-history-v1"  /* 15 bytes + implicit \0 */
#define LM_CHAT_AAD_LEN         16

/* --- Internal types --- */

typedef struct {
    uint16_t              channel_id;
    char                  path[1024];
    FILE                 *fp;               /* append-mode, stays open */
    lm_chat_idx_entry_t  *entries;
    size_t                count;
    size_t                capacity;
} lm_chat_channel_t;

typedef struct {
    uint64_t  id;
    /* sidecar-only: presence in the set => tombstoned */
} lm_chat_tombstone_t;

struct lm_chat_history_s {
    char                        base_dir[1024];
    char                        dir[1024];       /* <base_dir>/ChatHistory */
    lm_chat_history_config_t    cfg;
    lm_chat_channel_t           channels[LM_CHAT_MAX_CHANNELS];
    size_t                      channel_count;
    uint64_t                    next_id;

    /* Tombstones sidecar */
    char                        tomb_path[1024];
    lm_chat_tombstone_t        *tombstones;
    size_t                      tomb_count;
    size_t                      tomb_capacity;

    /* Encryption */
    int                         encryption_enabled;
    uint8_t                     key[LM_CHAT_KEY_LEN];
};

/* --- Small helpers --- */

static int lm_mkdir_p(const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0) return S_ISDIR(st.st_mode) ? 0 : -1;
    if (mkdir(path, 0755) == 0) return 0;
    return -1;
}

static int lm_idx_reserve(lm_chat_channel_t *ch, size_t need)
{
    if (need <= ch->capacity) return 0;
    size_t new_cap = ch->capacity ? ch->capacity : LM_CHAT_INDEX_GROW;
    while (new_cap < need) new_cap *= 2;
    lm_chat_idx_entry_t *n = (lm_chat_idx_entry_t *)realloc(
        ch->entries, new_cap * sizeof(lm_chat_idx_entry_t));
    if (!n) return -1;
    ch->entries = n;
    ch->capacity = new_cap;
    return 0;
}

static int lm_tomb_reserve(lm_chat_history_t *ctx, size_t need)
{
    if (need <= ctx->tomb_capacity) return 0;
    size_t new_cap = ctx->tomb_capacity ? ctx->tomb_capacity : 64;
    while (new_cap < need) new_cap *= 2;
    lm_chat_tombstone_t *n = (lm_chat_tombstone_t *)realloc(
        ctx->tombstones, new_cap * sizeof(lm_chat_tombstone_t));
    if (!n) return -1;
    ctx->tombstones = n;
    ctx->tomb_capacity = new_cap;
    return 0;
}

static int lm_tomb_has(const lm_chat_history_t *ctx, uint64_t id)
{
    for (size_t i = 0; i < ctx->tomb_count; ++i)
        if (ctx->tombstones[i].id == id) return 1;
    return 0;
}

/* Extract just "id":<number> from a JSONL line. Returns 0 on success. */
static int lm_parse_id(const char *line, uint64_t *out_id)
{
    const char *p = strstr(line, "\"id\":");
    if (!p) return -1;
    p += 5;
    while (*p == ' ') ++p;
    char *end;
    unsigned long long v = strtoull(p, &end, 10);
    if (end == p) return -1;
    *out_id = (uint64_t)v;
    return 0;
}

/* JSON-escape `in` into `out` (adds quotes around the value).
 * Returns bytes written (excluding NUL), or -1 if would exceed cap-1. */
static int lm_json_quote(char *out, size_t cap, const char *in)
{
    size_t o = 0;
    if (o + 1 >= cap) return -1;
    out[o++] = '"';
    for (const unsigned char *p = (const unsigned char *)in; *p; ++p) {
        const char *esc = NULL;
        char buf[8];
        switch (*p) {
            case '"':  esc = "\\\""; break;
            case '\\': esc = "\\\\"; break;
            case '\n': esc = "\\n";  break;
            case '\r': esc = "\\r";  break;
            case '\t': esc = "\\t";  break;
            case '\b': esc = "\\b";  break;
            case '\f': esc = "\\f";  break;
            default:
                if (*p < 0x20) {
                    snprintf(buf, sizeof(buf), "\\u%04x", (unsigned)*p);
                    esc = buf;
                }
                break;
        }
        if (esc) {
            size_t l = strlen(esc);
            if (o + l >= cap) return -1;
            memcpy(out + o, esc, l);
            o += l;
        } else {
            if (o + 1 >= cap) return -1;
            out[o++] = (char)*p;
        }
    }
    if (o + 1 >= cap) return -1;
    out[o++] = '"';
    out[o] = '\0';
    return (int)o;
}

/* Find the value for `"key":"..."` in line, copy into out.
 * Handles the small escape set we emit. Returns 0 on success. */
static int lm_json_get_string(const char *line, const char *key,
                              char *out, size_t cap)
{
    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\":\"", key);
    const char *p = strstr(line, needle);
    if (!p) { if (cap) out[0] = '\0'; return -1; }
    p += strlen(needle);
    size_t o = 0;
    while (*p && *p != '"') {
        if (*p == '\\' && p[1]) {
            char c = 0;
            switch (p[1]) {
                case '"':  c = '"'; break;
                case '\\': c = '\\'; break;
                case '/':  c = '/'; break;
                case 'n':  c = '\n'; break;
                case 'r':  c = '\r'; break;
                case 't':  c = '\t'; break;
                case 'b':  c = '\b'; break;
                case 'f':  c = '\f'; break;
                default:   c = p[1]; break;
            }
            if (o + 1 < cap) out[o++] = c;
            p += 2;
        } else {
            if (o + 1 < cap) out[o++] = *p;
            ++p;
        }
    }
    if (cap) out[o < cap ? o : cap - 1] = '\0';
    return 0;
}

static int lm_json_get_u64(const char *line, const char *key, uint64_t *out)
{
    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\":", key);
    const char *p = strstr(line, needle);
    if (!p) return -1;
    p += strlen(needle);
    while (*p == ' ') ++p;
    char *end;
    unsigned long long v = strtoull(p, &end, 10);
    if (end == p) return -1;
    *out = (uint64_t)v;
    return 0;
}

/* --- Base64 (minimal, for ENC: body encoding) --- */

static const char b64enc[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static size_t lm_b64_encode(const uint8_t *in, size_t in_len, char *out)
{
    size_t o = 0;
    size_t i = 0;
    while (i + 3 <= in_len) {
        uint32_t v = ((uint32_t)in[i] << 16) | ((uint32_t)in[i+1] << 8) | in[i+2];
        out[o++] = b64enc[(v >> 18) & 0x3F];
        out[o++] = b64enc[(v >> 12) & 0x3F];
        out[o++] = b64enc[(v >> 6)  & 0x3F];
        out[o++] = b64enc[v         & 0x3F];
        i += 3;
    }
    if (i < in_len) {
        uint32_t v = (uint32_t)in[i] << 16;
        if (i + 1 < in_len) v |= (uint32_t)in[i+1] << 8;
        out[o++] = b64enc[(v >> 18) & 0x3F];
        out[o++] = b64enc[(v >> 12) & 0x3F];
        out[o++] = (i + 1 < in_len) ? b64enc[(v >> 6) & 0x3F] : '=';
        out[o++] = '=';
    }
    out[o] = '\0';
    return o;
}

static int lm_b64_decode(const char *in, size_t in_len,
                        uint8_t *out, size_t out_cap, size_t *out_len)
{
    static int8_t tbl[256] = {0};
    static int init = 0;
    if (!init) {
        for (int i = 0; i < 256; ++i) tbl[i] = -1;
        for (int i = 0; i < 64; ++i) tbl[(unsigned)b64enc[i]] = (int8_t)i;
        tbl[(unsigned)'='] = 0;
        init = 1;
    }
    size_t o = 0;
    for (size_t i = 0; i < in_len; i += 4) {
        if (i + 3 >= in_len) return -1;
        int a = tbl[(unsigned)in[i]];
        int b = tbl[(unsigned)in[i+1]];
        int c = tbl[(unsigned)in[i+2]];
        int d = tbl[(unsigned)in[i+3]];
        if (a < 0 || b < 0 || c < 0 || d < 0) return -1;
        uint32_t v = ((uint32_t)a << 18) | ((uint32_t)b << 12) |
                     ((uint32_t)c << 6)  |  (uint32_t)d;
        if (o + 1 > out_cap) return -1;
        out[o++] = (v >> 16) & 0xFF;
        if (in[i+2] != '=') {
            if (o + 1 > out_cap) return -1;
            out[o++] = (v >> 8) & 0xFF;
        }
        if (in[i+3] != '=') {
            if (o + 1 > out_cap) return -1;
            out[o++] = v & 0xFF;
        }
    }
    *out_len = o;
    return 0;
}

/* --- Random bytes (nonce) --- */

static int lm_rand_bytes(uint8_t *out, size_t len)
{
    FILE *f = fopen("/dev/urandom", "rb");
    if (!f) return -1;
    size_t n = fread(out, 1, len, f);
    fclose(f);
    return n == len ? 0 : -1;
}

/* --- Channel path construction --- */

static void lm_channel_path(const lm_chat_history_t *ctx, uint16_t ch,
                           char *out, size_t cap)
{
    snprintf(out, cap, "%s/channel-%u.jsonl", ctx->dir, (unsigned)ch);
}

/* Scan a channel file, building its index, fixing crash damage.
 * Returns 0 on success. */
static int lm_channel_scan(lm_chat_channel_t *ch, uint64_t *max_id_out)
{
    FILE *f = fopen(ch->path, "rb");
    if (!f) {
        /* file doesn't exist yet — that's fine, empty channel */
        if (errno == ENOENT) return 0;
        return -1;
    }
    char line[LM_CHAT_LINE_MAX];
    long last_good = 0;
    int corrupted = 0;
    while (!feof(f)) {
        long start = ftello(f);
        if (!fgets(line, sizeof(line), f)) break;
        size_t len = strlen(line);
        /* Must end with '\n' to be a complete line */
        if (len == 0 || line[len - 1] != '\n') {
            corrupted = 1;
            break;
        }
        uint64_t id;
        if (lm_parse_id(line, &id) != 0) {
            corrupted = 1;
            break;
        }
        if (lm_idx_reserve(ch, ch->count + 1) != 0) {
            fclose(f);
            return -1;
        }
        ch->entries[ch->count].id = id;
        ch->entries[ch->count].offset = start;
        ch->entries[ch->count].length = (uint32_t)len;
        ch->count++;
        if (id > *max_id_out) *max_id_out = id;
        last_good = start + (long)len;
    }
    fclose(f);

    if (corrupted) {
        int fd = open(ch->path, O_WRONLY);
        if (fd < 0) return -1;
        if (ftruncate(fd, (off_t)last_good) != 0) {
            close(fd);
            return -1;
        }
        fsync(fd);
        close(fd);
    }

    return 0;
}

/* Scan the tombstones sidecar file. */
static int lm_tombstones_scan(lm_chat_history_t *ctx)
{
    FILE *f = fopen(ctx->tomb_path, "rb");
    if (!f) return 0;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        uint64_t id;
        if (lm_parse_id(line, &id) != 0) continue;
        if (lm_tomb_reserve(ctx, ctx->tomb_count + 1) != 0) {
            fclose(f);
            return -1;
        }
        ctx->tombstones[ctx->tomb_count].id = id;
        ctx->tomb_count++;
    }
    fclose(f);
    return 0;
}

/* Discover channel-N.jsonl files in the history dir and open/index each. */
static int lm_discover_channels(lm_chat_history_t *ctx, uint64_t *max_id)
{
    DIR *d = opendir(ctx->dir);
    if (!d) return -1;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        unsigned ch;
        if (sscanf(de->d_name, "channel-%u.jsonl", &ch) != 1) continue;
        if (ch >= LM_CHAT_MAX_CHANNELS) continue;
        if (ctx->channel_count >= LM_CHAT_MAX_CHANNELS) break;
        lm_chat_channel_t *c = &ctx->channels[ctx->channel_count++];
        c->channel_id = (uint16_t)ch;
        lm_channel_path(ctx, c->channel_id, c->path, sizeof(c->path));
        if (lm_channel_scan(c, max_id) != 0) {
            closedir(d);
            return -1;
        }
        c->fp = fopen(c->path, "ab");
        if (!c->fp) {
            closedir(d);
            return -1;
        }
    }
    closedir(d);
    return 0;
}

/* Get or create a channel slot. Opens the file in append mode. */
static lm_chat_channel_t *lm_channel_get_or_create(lm_chat_history_t *ctx,
                                                  uint16_t channel_id)
{
    for (size_t i = 0; i < ctx->channel_count; ++i)
        if (ctx->channels[i].channel_id == channel_id)
            return &ctx->channels[i];
    if (ctx->channel_count >= LM_CHAT_MAX_CHANNELS) return NULL;
    lm_chat_channel_t *c = &ctx->channels[ctx->channel_count++];
    memset(c, 0, sizeof(*c));
    c->channel_id = channel_id;
    lm_channel_path(ctx, channel_id, c->path, sizeof(c->path));
    c->fp = fopen(c->path, "ab");
    if (!c->fp) { ctx->channel_count--; return NULL; }
    return c;
}

/* Binary-search the index for the smallest entry with id >= target.
 * Returns index in [0, count]; count means "past end". */
static size_t lm_idx_lower_bound(const lm_chat_channel_t *ch, uint64_t target)
{
    size_t lo = 0, hi = ch->count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (ch->entries[mid].id < target) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

/* --- Encryption helpers --- */

static int lm_load_key(const char *path, uint8_t out[LM_CHAT_KEY_LEN])
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    size_t n = fread(out, 1, LM_CHAT_KEY_LEN, f);
    fclose(f);
    return n == LM_CHAT_KEY_LEN ? 0 : -1;
}

/* Encrypt plaintext body, produce "ENC:<base64(nonce||ciphertext||tag)>" in out. */
static int lm_body_encrypt(const uint8_t key[LM_CHAT_KEY_LEN],
                          const char *plaintext, char *out, size_t out_cap)
{
    size_t pt_len = strlen(plaintext);
    uint8_t nonce[LM_CHAT_NONCE_LEN];
    if (lm_rand_bytes(nonce, LM_CHAT_NONCE_LEN) != 0) return -1;

    size_t raw_len = LM_CHAT_NONCE_LEN + pt_len + LM_CHAT_TAG_LEN;
    uint8_t *raw = (uint8_t *)malloc(raw_len);
    if (!raw) return -1;
    memcpy(raw, nonce, LM_CHAT_NONCE_LEN);

    uint8_t *ct = raw + LM_CHAT_NONCE_LEN;
    uint8_t *tag = ct + pt_len;
    if (hl_chacha20_poly1305_encrypt(key, nonce,
                                     (const uint8_t *)plaintext, pt_len,
                                     ct, tag) != 0) {
        free(raw);
        return -1;
    }

    /* "ENC:" + base64 */
    if (out_cap < 4 + ((raw_len + 2) / 3) * 4 + 1) { free(raw); return -1; }
    memcpy(out, "ENC:", 4);
    lm_b64_encode(raw, raw_len, out + 4);
    free(raw);
    return 0;
}

/* Decrypt "ENC:<base64>" back to plaintext. Returns 0 on success. */
static int lm_body_decrypt(const uint8_t key[LM_CHAT_KEY_LEN],
                          const char *enc, char *out, size_t out_cap)
{
    if (strncmp(enc, "ENC:", 4) != 0) return -1;
    const char *b64 = enc + 4;
    size_t b64_len = strlen(b64);
    size_t raw_cap = (b64_len / 4) * 3 + 1;
    uint8_t *raw = (uint8_t *)malloc(raw_cap);
    if (!raw) return -1;
    size_t raw_len = 0;
    if (lm_b64_decode(b64, b64_len, raw, raw_cap, &raw_len) != 0) {
        free(raw);
        return -1;
    }
    if (raw_len < LM_CHAT_NONCE_LEN + LM_CHAT_TAG_LEN) { free(raw); return -1; }
    uint8_t *nonce = raw;
    uint8_t *ct = raw + LM_CHAT_NONCE_LEN;
    size_t ct_len = raw_len - LM_CHAT_NONCE_LEN - LM_CHAT_TAG_LEN;
    uint8_t *tag = ct + ct_len;
    if (out_cap < ct_len + 1) { free(raw); return -1; }
    if (hl_chacha20_poly1305_decrypt(key, nonce,
                                     ct, ct_len, tag,
                                     (uint8_t *)out) != 0) {
        free(raw);
        return -1;
    }
    out[ct_len] = '\0';
    free(raw);
    return 0;
}

/* --- Public API --- */

lm_chat_history_t *lm_chat_history_open(const char *base_dir,
                                        const lm_chat_history_config_t *cfg)
{
    if (!base_dir || !cfg) return NULL;

    lm_chat_history_t *ctx = (lm_chat_history_t *)calloc(1, sizeof(*ctx));
    if (!ctx) return NULL;

    strncpy(ctx->base_dir, base_dir, sizeof(ctx->base_dir) - 1);
    snprintf(ctx->dir, sizeof(ctx->dir), "%s/ChatHistory", base_dir);
    ctx->cfg = *cfg;
    ctx->next_id = 1;
    snprintf(ctx->tomb_path, sizeof(ctx->tomb_path),
             "%s/tombstones.jsonl", ctx->dir);

    if (lm_mkdir_p(ctx->dir) != 0) {
        free(ctx);
        return NULL;
    }

    /* Load encryption key if configured */
    if (cfg->encryption_key_path[0]) {
        if (lm_load_key(cfg->encryption_key_path, ctx->key) != 0) {
            free(ctx);
            return NULL;
        }
        ctx->encryption_enabled = 1;
    }

    uint64_t max_id = 0;
    if (lm_discover_channels(ctx, &max_id) != 0) {
        lm_chat_history_close(ctx);
        return NULL;
    }

    if (lm_tombstones_scan(ctx) != 0) {
        lm_chat_history_close(ctx);
        return NULL;
    }

    ctx->next_id = max_id + 1;
    return ctx;
}

uint64_t lm_chat_history_append(lm_chat_history_t *ctx,
                                uint16_t channel_id,
                                uint16_t flags,
                                uint16_t icon_id,
                                const char *nick,
                                const char *body)
{
    if (!ctx || !ctx->cfg.enabled) return 0;
    if (!nick) nick = "";
    if (!body) body = "";

    lm_chat_channel_t *ch = lm_channel_get_or_create(ctx, channel_id);
    if (!ch) return 0;

    uint64_t id = ctx->next_id++;
    time_t now = time(NULL);

    char nick_q[LM_CHAT_MAX_NICK_LEN * 2 + 4];
    if (lm_json_quote(nick_q, sizeof(nick_q), nick) < 0) return 0;

    /* Encrypt body if key loaded; otherwise plain-quote */
    char body_q[LM_CHAT_MAX_BODY_LEN * 2 + 256];
    if (ctx->encryption_enabled) {
        char enc[LM_CHAT_MAX_BODY_LEN * 2 + 128];
        if (lm_body_encrypt(ctx->key, body, enc, sizeof(enc)) != 0) return 0;
        if (lm_json_quote(body_q, sizeof(body_q), enc) < 0) return 0;
    } else {
        if (lm_json_quote(body_q, sizeof(body_q), body) < 0) return 0;
    }

    char line[LM_CHAT_LINE_MAX];
    int n = snprintf(line, sizeof(line),
        "{\"id\":%llu,\"ch\":%u,\"ts\":%lld,\"flags\":%u,\"icon\":%u,"
        "\"nick\":%s,\"body\":%s}\n",
        (unsigned long long)id,
        (unsigned)channel_id,
        (long long)now,
        (unsigned)flags,
        (unsigned)icon_id,
        nick_q,
        body_q);
    if (n <= 0 || (size_t)n >= sizeof(line)) return 0;

    long offset = ftello(ch->fp);
    if (offset < 0) return 0;
    if (fwrite(line, 1, (size_t)n, ch->fp) != (size_t)n) return 0;
    fflush(ch->fp);

    if (lm_idx_reserve(ch, ch->count + 1) != 0) return 0;
    ch->entries[ch->count].id = id;
    ch->entries[ch->count].offset = offset;
    ch->entries[ch->count].length = (uint32_t)n;
    ch->count++;

    return id;
}

/* Read a full line from the channel file at the given offset. */
static int lm_read_line_at(lm_chat_channel_t *ch, long offset, uint32_t length,
                          char *out, size_t out_cap)
{
    if (length + 1 > out_cap) return -1;
    FILE *f = fopen(ch->path, "rb");
    if (!f) return -1;
    if (fseeko(f, offset, SEEK_SET) != 0) { fclose(f); return -1; }
    size_t n = fread(out, 1, length, f);
    fclose(f);
    if (n != length) return -1;
    out[length] = '\0';
    return 0;
}

/* Populate an entry struct from a parsed JSONL line.
 * Applies tombstone and decryption transforms. */
static int lm_parse_entry(lm_chat_history_t *ctx, const char *line,
                         lm_chat_entry_t *e)
{
    memset(e, 0, sizeof(*e));
    uint64_t id = 0, ts = 0, ch = 0, flags = 0, icon = 0;
    if (lm_json_get_u64(line, "id", &id) != 0) return -1;
    lm_json_get_u64(line, "ch", &ch);
    lm_json_get_u64(line, "ts", &ts);
    lm_json_get_u64(line, "flags", &flags);
    lm_json_get_u64(line, "icon", &icon);
    e->id = id;
    e->channel_id = (uint16_t)ch;
    e->ts = (int64_t)ts;
    e->flags = (uint16_t)flags;
    e->icon_id = (uint16_t)icon;
    lm_json_get_string(line, "nick", e->nick, sizeof(e->nick));
    lm_json_get_string(line, "body", e->body, sizeof(e->body));

    /* Decrypt if body starts with "ENC:" */
    if (ctx->encryption_enabled && strncmp(e->body, "ENC:", 4) == 0) {
        char plain[LM_CHAT_MAX_BODY_LEN + 1];
        if (lm_body_decrypt(ctx->key, e->body, plain, sizeof(plain)) == 0) {
            strncpy(e->body, plain, sizeof(e->body) - 1);
            e->body[sizeof(e->body) - 1] = '\0';
        } else {
            strncpy(e->body, "[decryption failed]", sizeof(e->body) - 1);
        }
    }

    /* Apply tombstone */
    if (lm_tomb_has(ctx, e->id)) {
        e->flags |= HL_CHAT_FLAG_IS_DELETED;
        e->nick[0] = '\0';
        e->body[0] = '\0';
    }

    return 0;
}

int lm_chat_history_query(lm_chat_history_t *ctx,
                          uint16_t channel_id,
                          uint64_t before,
                          uint64_t after,
                          uint16_t limit,
                          lm_chat_entry_t **out_entries,
                          size_t *out_count,
                          uint8_t *out_has_more)
{
    if (!ctx || !out_entries || !out_count || !out_has_more) return -1;
    *out_entries = NULL;
    *out_count = 0;
    *out_has_more = 0;

    if (limit == 0 || limit > LM_CHAT_HISTORY_MAX_LIMIT)
        limit = LM_CHAT_HISTORY_DEFAULT_LIMIT;

    /* Find channel; if absent, return empty */
    lm_chat_channel_t *ch = NULL;
    for (size_t i = 0; i < ctx->channel_count; ++i)
        if (ctx->channels[i].channel_id == channel_id) {
            ch = &ctx->channels[i];
            break;
        }
    if (!ch || ch->count == 0) return 0;

    /* Determine the window [start_idx, end_idx) in the index.
     * Entries are sorted oldest-first by id. */
    size_t start_idx, end_idx;

    if (after && before) {
        /* Range query: ids in (after, before), oldest-first, capped */
        start_idx = lm_idx_lower_bound(ch, after + 1);
        end_idx   = lm_idx_lower_bound(ch, before);
        if (end_idx - start_idx > limit) {
            end_idx = start_idx + limit;
            *out_has_more = 1;
        }
    } else if (after) {
        /* AFTER only: smallest ids greater than `after`, oldest-first */
        start_idx = lm_idx_lower_bound(ch, after + 1);
        end_idx   = start_idx + limit;
        if (end_idx >= ch->count) end_idx = ch->count;
        else *out_has_more = 1;
    } else if (before) {
        /* BEFORE only: largest `limit` ids less than `before`, oldest-first */
        end_idx = lm_idx_lower_bound(ch, before);
        start_idx = (end_idx > limit) ? end_idx - limit : 0;
        if (start_idx > 0) *out_has_more = 1;
    } else {
        /* No cursor: most recent `limit` entries */
        end_idx = ch->count;
        start_idx = (end_idx > limit) ? end_idx - limit : 0;
        if (start_idx > 0) *out_has_more = 1;
    }

    size_t n = end_idx - start_idx;
    if (n == 0) return 0;

    lm_chat_entry_t *arr = (lm_chat_entry_t *)calloc(n, sizeof(lm_chat_entry_t));
    if (!arr) return -1;

    char line[LM_CHAT_LINE_MAX];
    size_t written = 0;
    for (size_t i = start_idx; i < end_idx; ++i) {
        if (lm_read_line_at(ch, ch->entries[i].offset,
                           ch->entries[i].length, line, sizeof(line)) != 0)
            continue;
        if (lm_parse_entry(ctx, line, &arr[written]) == 0)
            written++;
    }

    *out_entries = arr;
    *out_count = written;
    return 0;
}

void lm_chat_history_entries_free(lm_chat_entry_t *entries)
{
    free(entries);
}

int lm_chat_history_tombstone(lm_chat_history_t *ctx, uint64_t message_id)
{
    if (!ctx) return -1;
    if (lm_tomb_has(ctx, message_id)) return 0; /* idempotent */

    FILE *f = fopen(ctx->tomb_path, "ab");
    if (!f) return -1;
    fprintf(f, "{\"id\":%llu,\"deleted\":true}\n",
            (unsigned long long)message_id);
    fflush(f);
    fclose(f);

    if (lm_tomb_reserve(ctx, ctx->tomb_count + 1) != 0) return -1;
    ctx->tombstones[ctx->tomb_count].id = message_id;
    ctx->tomb_count++;
    return 0;
}

int lm_chat_history_prune(lm_chat_history_t *ctx)
{
    if (!ctx || !ctx->cfg.enabled) return 0;
    time_t now = time(NULL);
    uint32_t max_msgs = ctx->cfg.max_msgs;
    uint32_t max_days = ctx->cfg.max_days;
    uint32_t slack = max_msgs ? (max_msgs / 10) : 0;

    for (size_t ci = 0; ci < ctx->channel_count; ++ci) {
        lm_chat_channel_t *ch = &ctx->channels[ci];
        if (ch->count == 0) continue;

        /* Decide drop_count: count-based */
        size_t drop_count = 0;
        if (max_msgs && ch->count > max_msgs + slack)
            drop_count = ch->count - max_msgs;

        /* Age-based: find first index with ts within window */
        size_t age_drop = 0;
        if (max_days) {
            int64_t cutoff = (int64_t)now - (int64_t)max_days * 86400;
            char line[LM_CHAT_LINE_MAX];
            for (size_t i = 0; i < ch->count; ++i) {
                if (lm_read_line_at(ch, ch->entries[i].offset,
                                   ch->entries[i].length,
                                   line, sizeof(line)) != 0) break;
                uint64_t ts = 0;
                lm_json_get_u64(line, "ts", &ts);
                if ((int64_t)ts >= cutoff) { age_drop = i; break; }
                if (i + 1 == ch->count) age_drop = ch->count;
            }
        }
        if (age_drop > drop_count) drop_count = age_drop;
        if (drop_count == 0) continue;

        /* Rewrite: keep entries [drop_count, count) */
        char tmp_path[1200];
        snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", ch->path);
        FILE *dst = fopen(tmp_path, "wb");
        if (!dst) continue;

        /* Rebuild index as we go — into a new buffer */
        size_t kept = ch->count - drop_count;
        lm_chat_idx_entry_t *new_idx =
            (lm_chat_idx_entry_t *)calloc(kept, sizeof(lm_chat_idx_entry_t));
        if (!new_idx) { fclose(dst); unlink(tmp_path); continue; }

        char line[LM_CHAT_LINE_MAX];
        size_t k = 0;
        long new_off = 0;
        for (size_t i = drop_count; i < ch->count; ++i) {
            if (lm_read_line_at(ch, ch->entries[i].offset,
                               ch->entries[i].length,
                               line, sizeof(line)) != 0) continue;
            size_t len = strlen(line);
            if (fwrite(line, 1, len, dst) != len) break;
            new_idx[k].id = ch->entries[i].id;
            new_idx[k].offset = new_off;
            new_idx[k].length = (uint32_t)len;
            new_off += (long)len;
            k++;
        }
        fflush(dst);
        int dst_fd = fileno(dst);
        if (dst_fd >= 0) fsync(dst_fd);
        fclose(dst);

        /* Close the old fp before rename (safer on some filesystems) */
        if (ch->fp) { fclose(ch->fp); ch->fp = NULL; }
        if (rename(tmp_path, ch->path) != 0) {
            free(new_idx);
            ch->fp = fopen(ch->path, "ab");
            continue;
        }
        free(ch->entries);
        ch->entries = new_idx;
        ch->count = k;
        ch->capacity = kept;
        ch->fp = fopen(ch->path, "ab");
    }

    return 0;
}

void lm_chat_history_fsync(lm_chat_history_t *ctx)
{
    if (!ctx) return;
    for (size_t i = 0; i < ctx->channel_count; ++i) {
        if (ctx->channels[i].fp) {
            fflush(ctx->channels[i].fp);
            int fd = fileno(ctx->channels[i].fp);
            if (fd >= 0) fsync(fd);
        }
    }
}

void lm_chat_history_close(lm_chat_history_t *ctx)
{
    if (!ctx) return;
    for (size_t i = 0; i < ctx->channel_count; ++i) {
        if (ctx->channels[i].fp) fclose(ctx->channels[i].fp);
        free(ctx->channels[i].entries);
    }
    free(ctx->tombstones);
    free(ctx);
}

size_t lm_chat_history_count(const lm_chat_history_t *ctx, uint16_t channel_id)
{
    if (!ctx) return 0;
    for (size_t i = 0; i < ctx->channel_count; ++i)
        if (ctx->channels[i].channel_id == channel_id)
            return ctx->channels[i].count;
    return 0;
}

uint64_t lm_chat_history_next_id(const lm_chat_history_t *ctx)
{
    return ctx ? ctx->next_id : 0;
}

int lm_chat_rl_consume(uint16_t *tokens_x10,
                       uint64_t *last_refill_ms,
                       uint64_t  now_ms,
                       uint32_t  capacity_tokens,
                       uint32_t  refill_per_sec)
{
    if (!tokens_x10 || !last_refill_ms) return 0;
    if (capacity_tokens == 0) capacity_tokens = 20;
    if (refill_per_sec == 0)  refill_per_sec  = 10;

    uint32_t cap_x10 = capacity_tokens * 10;
    if (cap_x10 > 0xFFFF) cap_x10 = 0xFFFF;

    /* Lazy seed: first call gets a full bucket. */
    if (*last_refill_ms == 0) {
        *tokens_x10     = (uint16_t)cap_x10;
        *last_refill_ms = now_ms;
    } else {
        uint64_t delta_ms = (now_ms >= *last_refill_ms)
                          ? (now_ms - *last_refill_ms) : 0;
        /* refill_x10 = delta_ms × refill_per_sec / 100  (tokens stored ×10) */
        uint64_t refill_x10 = (delta_ms * (uint64_t)refill_per_sec) / 100ULL;
        if (refill_x10 > 0) {
            uint32_t total = (uint32_t)*tokens_x10 + (uint32_t)refill_x10;
            if (total > cap_x10) total = cap_x10;
            *tokens_x10     = (uint16_t)total;
            *last_refill_ms = now_ms;
        }
    }

    if (*tokens_x10 >= 10) {
        *tokens_x10 = (uint16_t)(*tokens_x10 - 10);
        return 1;
    }
    return 0;
}
