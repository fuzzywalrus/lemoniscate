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

    /* Mnemosyne defaults: enabled per-content-type when URL is set */
    cfg->mnemosyne_index_files = 1;
    cfg->mnemosyne_index_news = 1;
}
