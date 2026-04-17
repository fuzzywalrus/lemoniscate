/*
 * config_loader.h - YAML configuration loader
 *
 * Maps to: internal/mobius/config.go
 *
 * Loads config.yaml and populates an hl_config_t struct.
 * Requires libyaml.
 */

#ifndef MOBIUS_CONFIG_LOADER_H
#define MOBIUS_CONFIG_LOADER_H

#include "hotline/config.h"

/* Config search paths — maps to Go ConfigSearchOrder */
#define MOBIUS_CONFIG_SEARCH_COUNT 3
extern const char *mobius_config_search_paths[MOBIUS_CONFIG_SEARCH_COUNT];

/*
 * mobius_load_config - Load config from a YAML file.
 * Maps to: Go LoadConfig()
 *
 * If config_dir is NULL, searches the default paths.
 * Returns 0 on success, -1 on error.
 */
int mobius_load_config(hl_config_t *cfg, const char *config_dir);

/*
 * mobius_find_config_dir - Find a config directory that exists.
 * Returns a static string or NULL if none found.
 */
const char *mobius_find_config_dir(void);

#endif /* MOBIUS_CONFIG_LOADER_H */
