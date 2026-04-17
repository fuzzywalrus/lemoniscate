/*
 * chat_history.h - Chat History extension
 *
 * Server-side persistence of public chat messages with cursor-based
 * pagination. See docs/Capabilities-Chat-History.md and
 * openspec/changes/chat-history/ for the full protocol and design.
 *
 * This header defines both the protocol constants (field IDs, flag bits)
 * and the storage interface (lm_chat_history_t). Following the HOPE
 * convention of placing protocol extensions in their own header.
 */

#ifndef HOTLINE_CHAT_HISTORY_H
#define HOTLINE_CHAT_HISTORY_H

#include "hotline/types.h"
#include "hotline/field.h"
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>   /* ssize_t */

/* --- Protocol field IDs (0x0F01–0x0F1F reserved for this extension) --- */

static const hl_field_type_t FIELD_CHANNEL_ID         = {0x0F, 0x01}; /* 3841 */
static const hl_field_type_t FIELD_HISTORY_BEFORE     = {0x0F, 0x02}; /* 3842 */
static const hl_field_type_t FIELD_HISTORY_AFTER      = {0x0F, 0x03}; /* 3843 */
static const hl_field_type_t FIELD_HISTORY_LIMIT      = {0x0F, 0x04}; /* 3844 */
static const hl_field_type_t FIELD_HISTORY_ENTRY      = {0x0F, 0x05}; /* 3845 */
static const hl_field_type_t FIELD_HISTORY_HAS_MORE   = {0x0F, 0x06}; /* 3846 */
static const hl_field_type_t FIELD_HISTORY_MAX_MSGS   = {0x0F, 0x07}; /* 3847 */
static const hl_field_type_t FIELD_HISTORY_MAX_DAYS   = {0x0F, 0x08}; /* 3848 */

/* --- History entry flag bits (uint16 bitfield inside each entry) --- */

#define HL_CHAT_FLAG_IS_ACTION      0x0001  /* /me emote */
#define HL_CHAT_FLAG_IS_SERVER_MSG  0x0002  /* admin broadcast / server msg */
#define HL_CHAT_FLAG_IS_DELETED     0x0004  /* tombstone */

/* --- Limits --- */

#define LM_CHAT_HISTORY_DEFAULT_LIMIT   50
#define LM_CHAT_HISTORY_MAX_LIMIT       200
#define LM_CHAT_MAX_BODY_LEN            2048
#define LM_CHAT_MAX_NICK_LEN            64

/* --- Storage interface --- */

typedef struct lm_chat_history_s lm_chat_history_t;

/*
 * A single entry returned by lm_chat_history_query().
 * Caller owns nothing — the storage layer allocates and frees the array
 * via lm_chat_history_entries_free().
 */
typedef struct {
    uint64_t  id;
    int64_t   ts;         /* Unix epoch seconds */
    uint16_t  flags;
    uint16_t  icon_id;
    char      nick[LM_CHAT_MAX_NICK_LEN + 1];  /* UTF-8, NUL-terminated */
    char      body[LM_CHAT_MAX_BODY_LEN + 1];  /* UTF-8, NUL-terminated */
    uint16_t  channel_id;
} lm_chat_entry_t;

/*
 * In-memory index entry — one per persisted message, sorted by id.
 * Not exposed to callers; kept in the header for testability.
 */
typedef struct {
    uint64_t  id;
    long      offset;     /* ftello() into channel-N.jsonl */
    uint32_t  length;     /* line length in bytes (incl. newline) */
} lm_chat_idx_entry_t;

/* Configuration passed to open. Mirrors the relevant hl_server_config_t fields. */
typedef struct {
    int       enabled;
    uint32_t  max_msgs;       /* 0 = unlimited */
    uint32_t  max_days;       /* 0 = unlimited */
    int       legacy_broadcast;
    uint32_t  legacy_count;
    char      encryption_key_path[1024];
} lm_chat_history_config_t;

/*
 * Open (or create) the ChatHistory/ directory under base_dir.
 * Scans existing channel files, builds per-channel in-memory indexes,
 * seeds the global next_id counter, and trims any partial last-line
 * crash damage via ftruncate. Returns NULL on fatal error.
 */
lm_chat_history_t *lm_chat_history_open(const char *base_dir,
                                        const lm_chat_history_config_t *cfg);

/*
 * Append a new message to the given channel. Returns the assigned
 * message ID (> 0) on success, 0 on failure. icon_id 0 = no icon.
 * nick and body are UTF-8; the server is responsible for transcoding
 * from the on-wire encoding (Mac Roman / UTF-8) before calling.
 */
uint64_t lm_chat_history_append(lm_chat_history_t *ctx,
                                uint16_t channel_id,
                                uint16_t flags,
                                uint16_t icon_id,
                                const char *nick,
                                const char *body);

/*
 * Query history for a channel. Returns an oldest-first array via
 * out_entries / out_count; sets *out_has_more per spec semantics.
 * Caller must lm_chat_history_entries_free() the returned array.
 * before = 0 means "no BEFORE cursor"; after = 0 means "no AFTER cursor".
 * limit is clamped to LM_CHAT_HISTORY_MAX_LIMIT.
 * Returns 0 on success, -1 on error.
 */
int lm_chat_history_query(lm_chat_history_t *ctx,
                          uint16_t channel_id,
                          uint64_t before,
                          uint64_t after,
                          uint16_t limit,
                          lm_chat_entry_t **out_entries,
                          size_t *out_count,
                          uint8_t *out_has_more);

void lm_chat_history_entries_free(lm_chat_entry_t *entries);

/*
 * Mark a message as tombstoned. Writes an entry to the sidecar
 * tombstones.jsonl; subsequent queries return the entry with
 * HL_CHAT_FLAG_IS_DELETED set and body/nick cleared.
 * Returns 0 on success, -1 if message not found.
 */
int lm_chat_history_tombstone(lm_chat_history_t *ctx, uint64_t message_id);

/*
 * Prune all channels per the configured retention policy.
 * Safe to call from a timer; self-contained (open/rewrite/rename/reindex).
 * Returns 0 on success.
 */
int lm_chat_history_prune(lm_chat_history_t *ctx);

/*
 * Periodic fsync of channel files. Called from server timer loop to bound
 * the power-loss window without paying fsync cost on every write.
 */
void lm_chat_history_fsync(lm_chat_history_t *ctx);

/*
 * Close the context and free all resources.
 */
void lm_chat_history_close(lm_chat_history_t *ctx);

/*
 * Introspection helpers (for tests and legacy-broadcast path).
 */
size_t lm_chat_history_count(const lm_chat_history_t *ctx, uint16_t channel_id);
uint64_t lm_chat_history_next_id(const lm_chat_history_t *ctx);

/*
 * lm_chat_rl_consume - Token-bucket attempt-to-consume.
 *
 * Pure (no I/O, no time syscall) so tests can drive `now_ms`
 * deterministically. Tokens are stored ×10 for sub-token resolution
 * (one request consumes 10 units = 1 token).
 *
 * Refills tokens up to `capacity_tokens × 10` based on the elapsed
 * `now_ms - *last_refill_ms`, then tries to consume 10. Updates
 * *tokens_x10 and *last_refill_ms in place. Returns 1 if the request
 * was allowed (token consumed), 0 if rate-limited.
 *
 * If *last_refill_ms is 0 on entry, the bucket is seeded full and
 * the call is allowed immediately (lazy initialization).
 */
int lm_chat_rl_consume(uint16_t *tokens_x10,
                       uint64_t *last_refill_ms,
                       uint64_t  now_ms,
                       uint32_t  capacity_tokens,
                       uint32_t  refill_per_sec);

#endif /* HOTLINE_CHAT_HISTORY_H */
