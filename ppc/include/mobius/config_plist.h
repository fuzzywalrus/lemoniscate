/*
 * config_plist.h - macOS plist configuration loader
 *
 * Reads server configuration from a standard macOS property list file
 * using CoreFoundation APIs (available on Tiger 10.4+).
 */

#ifndef MOBIUS_CONFIG_PLIST_H
#define MOBIUS_CONFIG_PLIST_H

#include "hotline/config.h"

/*
 * mobius_load_config_plist - Load configuration from a plist file.
 * Returns 0 on success, -1 if file not found or parse error.
 * On failure, cfg is left at its current (default) values.
 */
int mobius_load_config_plist(hl_config_t *cfg, const char *plist_path);

#endif /* MOBIUS_CONFIG_PLIST_H */
