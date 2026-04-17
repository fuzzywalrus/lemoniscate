/*
 * config.c - Server configuration defaults
 *
 * Maps to: hotline/config.go
 */

#include "hotline/config.h"
#include <string.h>

void hl_config_init(hl_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    strncpy(cfg->name, "Lemoniscate Server", HL_CONFIG_NAME_MAX);
    strncpy(cfg->description, "A Hotline server", HL_CONFIG_DESC_MAX);
    strncpy(cfg->encoding, "macintosh", sizeof(cfg->encoding) - 1);
    cfg->max_downloads = 0;           /* 0 = unlimited */
    cfg->max_downloads_per_client = 0;
    cfg->max_connections_per_ip = 0;
    strncpy(cfg->hope_required_prefix, "[E2E]", sizeof(cfg->hope_required_prefix) - 1);
    strncpy(cfg->hope_cipher_policy, "prefer-aead", sizeof(cfg->hope_cipher_policy) - 1);
    cfg->e2e_require_aead = 0;

    /* Mnemosyne defaults: enabled per-content-type when URL is set */
    cfg->mnemosyne_index_files = 1;
    cfg->mnemosyne_index_news = 1;
    cfg->mnemosyne_index_msgboard = 1;

    /* Chat history defaults — feature is opt-in, conservative retention/rate */
    cfg->chat_history_enabled = 0;
    cfg->chat_history_max_msgs = 10000;
    cfg->chat_history_max_days = 0;
    cfg->chat_history_legacy_broadcast = 0;
    cfg->chat_history_legacy_count = 30;
    cfg->chat_history_rate_capacity = 20;
    cfg->chat_history_rate_refill_per_sec = 10;
    cfg->chat_history_log_joins = 0;
}
