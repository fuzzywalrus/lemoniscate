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
    int done = 0;

    while (!done) {
        if (!yaml_parser_parse(&parser, &event)) {
            yaml_parser_delete(&parser);
            fclose(f);
            return -1;
        }

        switch (event.type) {
        case YAML_MAPPING_START_EVENT:
            in_mapping = 1;
            break;

        case YAML_MAPPING_END_EVENT:
            in_mapping = 0;
            break;

        case YAML_SCALAR_EVENT:
            if (parsing_trackers) {
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

    return parse_config_yaml(cfg, filepath);
}
