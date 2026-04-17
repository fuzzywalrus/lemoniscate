/*
 * chat_history.h - Chat History extension
 *
 * Server-side persistence of public chat messages with cursor-based
 * pagination.
 */

#ifndef HOTLINE_CHAT_HISTORY_H
#define HOTLINE_CHAT_HISTORY_H

#include "hotline/types.h"
#include "hotline/field.h"
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

/* --- Protocol field IDs (0x0F01-0x0F1F reserved for this extension) --- */

static const hl_field_type_t FIELD_CHANNEL_ID         = {0x0F, 0x01}; /* 3841 */
static const hl_field_type_t FIELD_HISTORY_BEFORE     = {0x0F, 0x02}; /* 3842 */
static const hl_field_type_t FIELD_HISTORY_AFTER      = {0x0F, 0x03}; /* 3843 */
static const hl_field_type_t FIELD_HISTORY_LIMIT      = {0x0F, 0x04}; /* 3844 */
static const hl_field_type_t FIELD_HISTORY_ENTRY      = {0x0F, 0x05}; /* 3845 */
static const hl_field_type_t FIELD_HISTORY_HAS_MORE   = {0x0F, 0x06}; /* 3846 */
static const hl_field_type_t FIELD_HISTORY_MAX_MSGS   = {0x0F, 0x07}; /* 3847 */
static const hl_field_type_t FIELD_HISTORY_MAX_DAYS   = {0x0F, 0x08}; /* 3848 */

/* --- History entry flag bits (uint16 bitfield inside each entry) --- */

#define HL_CHAT_FLAG_IS_ACTION      0x0001
#define HL_CHAT_FLAG_IS_SERVER_MSG  0x0002
#define HL_CHAT_FLAG_IS_DELETED     0x0004

/* --- Limits --- */

#define LM_CHAT_HISTORY_DEFAULT_LIMIT   50
#define LM_CHAT_HISTORY_MAX_LIMIT       200
#define LM_CHAT_MAX_BODY_LEN            2048
#define LM_CHAT_MAX_NICK_LEN            64

/* --- Storage interface --- */

typedef struct lm_chat_history_s lm_chat_history_t;

typedef struct {
    uint64_t  id;
    int64_t   ts;
    uint16_t  flags;
    uint16_t  icon_id;
    char      nick[LM_CHAT_MAX_NICK_LEN + 1];
    char      body[LM_CHAT_MAX_BODY_LEN + 1];
    uint16_t  channel_id;
} lm_chat_entry_t;

typedef struct {
    uint64_t  id;
    long      offset;
    uint32_t  length;
} lm_chat_idx_entry_t;

typedef struct {
    int       enabled;
    uint32_t  max_msgs;
    uint32_t  max_days;
    int       legacy_broadcast;
    uint32_t  legacy_count;
    char      encryption_key_path[1024];
} lm_chat_history_config_t;

lm_chat_history_t *lm_chat_history_open(const char *base_dir,
                                        const lm_chat_history_config_t *cfg);

uint64_t lm_chat_history_append(lm_chat_history_t *ctx,
                                uint16_t channel_id,
                                uint16_t flags,
                                uint16_t icon_id,
                                const char *nick,
                                const char *body);

int lm_chat_history_query(lm_chat_history_t *ctx,
                          uint16_t channel_id,
                          uint64_t before,
                          uint64_t after,
                          uint16_t limit,
                          lm_chat_entry_t **out_entries,
                          size_t *out_count,
                          uint8_t *out_has_more);

void lm_chat_history_entries_free(lm_chat_entry_t *entries);

int lm_chat_history_tombstone(lm_chat_history_t *ctx, uint64_t message_id);

int lm_chat_history_prune(lm_chat_history_t *ctx);

void lm_chat_history_fsync(lm_chat_history_t *ctx);

void lm_chat_history_close(lm_chat_history_t *ctx);

size_t lm_chat_history_count(const lm_chat_history_t *ctx, uint16_t channel_id);
uint64_t lm_chat_history_next_id(const lm_chat_history_t *ctx);

int lm_chat_rl_consume(uint16_t *tokens_x10,
                       uint64_t *last_refill_ms,
                       uint64_t  now_ms,
                       uint32_t  capacity_tokens,
                       uint32_t  refill_per_sec);

#endif /* HOTLINE_CHAT_HISTORY_H */
