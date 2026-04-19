/*
 * config_loader.c - YAML configuration loader
 *
 * Maps to: internal/mobius/config.go
 *
 * Uses libyaml's event-based parser to populate hl_config_t.
 */

#include "mobius/config_loader.h"
#include <yaml.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* Parse YAML boolean values: true/True/TRUE/yes/Yes/YES all → 1 */
static int yaml_parse_bool(const char *val)
{
    return (strcasecmp(val, "true") == 0 || strcasecmp(val, "yes") == 0);
}

/* Parse "#RRGGBB" (case-insensitive) into 0x00RRGGBB. Empty string or
 * invalid format returns 0xFFFFFFFF (meaning "no color"). Used by the
 * ColoredNicknames config section. */
static uint32_t yaml_parse_color_hex(const char *val)
{
    if (!val || val[0] == '\0') return 0xFFFFFFFFu;
    const char *p = val;
    if (*p == '#') p++;
    if (strlen(p) != 6) {
        fprintf(stderr, "config: invalid Color value '%s' (expected #RRGGBB), treating as no color\n", val);
        return 0xFFFFFFFFu;
    }
    uint32_t result = 0;
    int i;
    for (i = 0; i < 6; i++) {
        char c = p[i];
        int nibble;
        if (c >= '0' && c <= '9') nibble = c - '0';
        else if (c >= 'a' && c <= 'f') nibble = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') nibble = c - 'A' + 10;
        else {
            fprintf(stderr, "config: invalid hex in Color '%s', treating as no color\n", val);
            return 0xFFFFFFFFu;
        }
        result = (result << 4) | (uint32_t)nibble;
    }
    return result & 0x00FFFFFFu;
}

const char *mobius_config_search_paths[MOBIUS_CONFIG_SEARCH_COUNT] = {
    "config",
    "/usr/local/var/mobius/config",
    "/opt/homebrew/var/mobius/config"
};

const char *mobius_find_config_dir(void)
{
    int i;
    struct stat st;
    for (i = 0; i < MOBIUS_CONFIG_SEARCH_COUNT; i++) {
        if (stat(mobius_config_search_paths[i], &st) == 0 && S_ISDIR(st.st_mode)) {
            return mobius_config_search_paths[i];
        }
    }
    return NULL;
}

/* Parse a YAML mapping into the config struct.
 * Uses libyaml event-based API for Tiger compatibility. */
static int parse_config_yaml(hl_config_t *cfg, const char *filepath)
{
    FILE *f = fopen(filepath, "rb");
    if (!f) return -1;

    yaml_parser_t parser;
    yaml_event_t event;

    if (!yaml_parser_initialize(&parser)) {
        fclose(f);
        return -1;
    }
    yaml_parser_set_input_file(&parser, f);

    /* State machine for parsing top-level key-value pairs */
    int in_mapping = 0;
    char current_key[128] = {0};
    int parsing_trackers = 0;
    int parsing_mnemosyne = 0;
    char mnemosyne_key[128] = {0};
    int parsing_chat_history = 0;
    char chat_history_key[64] = {0};
    int parsing_colored_nicknames = 0;
    char colored_nicknames_key[64] = {0};
    int done = 0;

    while (!done) {
        if (!yaml_parser_parse(&parser, &event)) {
            yaml_parser_delete(&parser);
            fclose(f);
            return -1;
        }

        switch (event.type) {
        case YAML_MAPPING_START_EVENT:
            if (in_mapping && strcmp(current_key, "Mnemosyne") == 0) {
                parsing_mnemosyne = 1;
                mnemosyne_key[0] = '\0';
                current_key[0] = '\0';
            } else if (in_mapping && strcmp(current_key, "ChatHistory") == 0) {
                parsing_chat_history = 1;
                chat_history_key[0] = '\0';
                current_key[0] = '\0';
            } else if (in_mapping && strcmp(current_key, "ColoredNicknames") == 0) {
                parsing_colored_nicknames = 1;
                colored_nicknames_key[0] = '\0';
                current_key[0] = '\0';
                /* Section present but keys absent defaults to auto. Only
                 * set delivery=auto if it is still at the section-absent
                 * default (off). If the user explicitly wrote
                 * Delivery: off, that value wins because the scalar
                 * branch below overwrites it. */
                if (cfg->colored_nicknames.delivery == HL_CN_DELIVERY_OFF) {
                    cfg->colored_nicknames.delivery = HL_CN_DELIVERY_AUTO;
                }
            } else {
                in_mapping = 1;
            }
            break;

        case YAML_MAPPING_END_EVENT:
            if (parsing_mnemosyne) {
                parsing_mnemosyne = 0;
            } else if (parsing_chat_history) {
                parsing_chat_history = 0;
            } else if (parsing_colored_nicknames) {
                parsing_colored_nicknames = 0;
            } else {
                in_mapping = 0;
            }
            break;

        case YAML_SCALAR_EVENT:
            if (parsing_mnemosyne) {
                /* Inside Mnemosyne mapping — key/value pairs */
                const char *val = (const char *)event.data.scalar.value;
                if (mnemosyne_key[0] == '\0') {
                    strncpy(mnemosyne_key, val, sizeof(mnemosyne_key) - 1);
                } else {
                    if (strcmp(mnemosyne_key, "url") == 0)
                        strncpy(cfg->mnemosyne_url, val,
                                HL_CONFIG_MNEMOSYNE_URL_MAX - 1);
                    else if (strcmp(mnemosyne_key, "api_key") == 0)
                        strncpy(cfg->mnemosyne_api_key, val,
                                HL_CONFIG_MNEMOSYNE_KEY_MAX - 1);
                    else if (strcmp(mnemosyne_key, "index_files") == 0)
                        cfg->mnemosyne_index_files = yaml_parse_bool(val);
                    else if (strcmp(mnemosyne_key, "index_news") == 0)
                        cfg->mnemosyne_index_news = yaml_parse_bool(val);
                    else if (strcmp(mnemosyne_key, "index_msgboard") == 0)
                        cfg->mnemosyne_index_msgboard = yaml_parse_bool(val);
                    mnemosyne_key[0] = '\0';
                }
            } else if (parsing_chat_history) {
                /* Inside ChatHistory mapping — key/value pairs */
                const char *val = (const char *)event.data.scalar.value;
                if (chat_history_key[0] == '\0') {
                    strncpy(chat_history_key, val, sizeof(chat_history_key) - 1);
                } else {
                    if (strcmp(chat_history_key, "Enabled") == 0)
                        cfg->chat_history_enabled = yaml_parse_bool(val);
                    else if (strcmp(chat_history_key, "MaxMessages") == 0)
                        cfg->chat_history_max_msgs = (uint32_t)strtoul(val, NULL, 10);
                    else if (strcmp(chat_history_key, "MaxDays") == 0)
                        cfg->chat_history_max_days = (uint32_t)strtoul(val, NULL, 10);
                    else if (strcmp(chat_history_key, "LegacyBroadcast") == 0)
                        cfg->chat_history_legacy_broadcast = yaml_parse_bool(val);
                    else if (strcmp(chat_history_key, "LegacyCount") == 0)
                        cfg->chat_history_legacy_count = (uint32_t)strtoul(val, NULL, 10);
                    else if (strcmp(chat_history_key, "EncryptionKey") == 0)
                        strncpy(cfg->chat_history_encryption_key_path, val,
                                sizeof(cfg->chat_history_encryption_key_path) - 1);
                    else if (strcmp(chat_history_key, "RateCapacity") == 0)
                        cfg->chat_history_rate_capacity = (uint32_t)strtoul(val, NULL, 10);
                    else if (strcmp(chat_history_key, "RateRefillPerSec") == 0)
                        cfg->chat_history_rate_refill_per_sec = (uint32_t)strtoul(val, NULL, 10);
                    else if (strcmp(chat_history_key, "LogJoins") == 0)
                        cfg->chat_history_log_joins = yaml_parse_bool(val);
                    chat_history_key[0] = '\0';
                }
            } else if (parsing_colored_nicknames) {
                /* Inside ColoredNicknames mapping — key/value pairs */
                const char *val = (const char *)event.data.scalar.value;
                if (colored_nicknames_key[0] == '\0') {
                    strncpy(colored_nicknames_key, val,
                            sizeof(colored_nicknames_key) - 1);
                } else {
                    if (strcmp(colored_nicknames_key, "Delivery") == 0) {
                        if (strcasecmp(val, "off") == 0)
                            cfg->colored_nicknames.delivery = HL_CN_DELIVERY_OFF;
                        else if (strcasecmp(val, "auto") == 0)
                            cfg->colored_nicknames.delivery = HL_CN_DELIVERY_AUTO;
                        else if (strcasecmp(val, "always") == 0)
                            cfg->colored_nicknames.delivery = HL_CN_DELIVERY_ALWAYS;
                        else {
                            fprintf(stderr, "config: unknown ColoredNicknames.Delivery '%s', treating as off\n", val);
                            cfg->colored_nicknames.delivery = HL_CN_DELIVERY_OFF;
                        }
                    } else if (strcmp(colored_nicknames_key, "HonorClientColors") == 0) {
                        cfg->colored_nicknames.honor_client_colors = yaml_parse_bool(val);
                    } else if (strcmp(colored_nicknames_key, "DefaultAdminColor") == 0) {
                        cfg->colored_nicknames.default_admin_color = yaml_parse_color_hex(val);
                    } else if (strcmp(colored_nicknames_key, "DefaultGuestColor") == 0) {
                        cfg->colored_nicknames.default_guest_color = yaml_parse_color_hex(val);
                    }
                    colored_nicknames_key[0] = '\0';
                }
            } else if (parsing_trackers) {
                /* Inside Trackers sequence — each scalar is a tracker entry */
                const char *val = (const char *)event.data.scalar.value;
                if (cfg->tracker_count < HL_CONFIG_MAX_TRACKERS) {
                    strncpy(cfg->trackers[cfg->tracker_count], val,
                            HL_CONFIG_TRACKER_LEN - 1);
                    cfg->tracker_count++;
                }
            } else if (in_mapping && current_key[0] == '\0') {
                /* This is a key */
                strncpy(current_key, (const char *)event.data.scalar.value,
                        sizeof(current_key) - 1);
            } else if (in_mapping && current_key[0] != '\0') {
                /* This is a value */
                const char *val = (const char *)event.data.scalar.value;

                if (strcmp(current_key, "Name") == 0)
                    strncpy(cfg->name, val, HL_CONFIG_NAME_MAX);
                else if (strcmp(current_key, "Description") == 0)
                    strncpy(cfg->description, val, HL_CONFIG_DESC_MAX);
                else if (strcmp(current_key, "BannerFile") == 0)
                    strncpy(cfg->banner_file, val, HL_CONFIG_PATH_MAX - 1);
                else if (strcmp(current_key, "FileRoot") == 0)
                    strncpy(cfg->file_root, val, HL_CONFIG_PATH_MAX - 1);
                else if (strcmp(current_key, "EnableTrackerRegistration") == 0)
                    cfg->enable_tracker_registration = yaml_parse_bool(val);
                else if (strcmp(current_key, "NewsDelimiter") == 0)
                    strncpy(cfg->news_delimiter, val, sizeof(cfg->news_delimiter) - 1);
                else if (strcmp(current_key, "NewsDateFormat") == 0)
                    strncpy(cfg->news_date_format, val, sizeof(cfg->news_date_format) - 1);
                else if (strcmp(current_key, "MaxDownloads") == 0)
                    cfg->max_downloads = atoi(val);
                else if (strcmp(current_key, "MaxDownloadsPerClient") == 0)
                    cfg->max_downloads_per_client = atoi(val);
                else if (strcmp(current_key, "MaxConnectionsPerIP") == 0)
                    cfg->max_connections_per_ip = atoi(val);
                else if (strcmp(current_key, "PreserveResourceForks") == 0)
                    cfg->preserve_resource_forks = yaml_parse_bool(val);
                else if (strcmp(current_key, "EnableBonjour") == 0)
                    cfg->enable_bonjour = yaml_parse_bool(val);
                else if (strcmp(current_key, "Encoding") == 0)
                    strncpy(cfg->encoding, val, sizeof(cfg->encoding) - 1);
                else if (strcmp(current_key, "EnableHOPE") == 0)
                    cfg->enable_hope = yaml_parse_bool(val);
                else if (strcmp(current_key, "HOPELegacyMode") == 0)
                    cfg->hope_legacy_mode = yaml_parse_bool(val);
                else if (strcmp(current_key, "HOPERequiredPrefix") == 0)
                    strncpy(cfg->hope_required_prefix, val,
                            sizeof(cfg->hope_required_prefix) - 1);
                else if (strcmp(current_key, "E2ERequireTLS") == 0)
                    cfg->e2e_require_tls = yaml_parse_bool(val);
                else if (strcmp(current_key, "HOPECipherPolicy") == 0)
                    strncpy(cfg->hope_cipher_policy, val,
                            sizeof(cfg->hope_cipher_policy) - 1);
                else if (strcmp(current_key, "E2ERequireAEAD") == 0)
                    cfg->e2e_require_aead = yaml_parse_bool(val);
                else if (strcmp(current_key, "TLSCertFile") == 0)
                    strncpy(cfg->tls_cert_path, val, HL_CONFIG_PATH_MAX - 1);
                else if (strcmp(current_key, "TLSKeyFile") == 0)
                    strncpy(cfg->tls_key_path, val, HL_CONFIG_PATH_MAX - 1);
                else if (strcmp(current_key, "TLSPort") == 0)
                    cfg->tls_port = atoi(val);

                current_key[0] = '\0';
            }
            break;

        case YAML_SEQUENCE_START_EVENT:
            if (strcmp(current_key, "Trackers") == 0) {
                parsing_trackers = 1;
            }
            break;

        case YAML_SEQUENCE_END_EVENT:
            parsing_trackers = 0;
            current_key[0] = '\0';
            break;

        case YAML_STREAM_END_EVENT:
            done = 1;
            break;

        default:
            break;
        }

        yaml_event_delete(&event);
    }

    yaml_parser_delete(&parser);
    fclose(f);
    return 0;
}

int mobius_load_config(hl_config_t *cfg, const char *config_dir)
{
    hl_config_init(cfg);

    const char *dir = config_dir;
    if (!dir) {
        dir = mobius_find_config_dir();
        if (!dir) return -1;
    }

    char filepath[2048];
    snprintf(filepath, sizeof(filepath), "%s/config.yaml", dir);

    int rc = parse_config_yaml(cfg, filepath);
    if (rc != 0)
        return rc;

    /* Validate Mnemosyne config: url requires api_key */
    if (cfg->mnemosyne_url[0] != '\0' && cfg->mnemosyne_api_key[0] == '\0') {
        fprintf(stderr, "Warning: Mnemosyne url set but api_key missing — sync disabled\n");
        cfg->mnemosyne_url[0] = '\0';
    }

    return 0;
}
