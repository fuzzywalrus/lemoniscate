/*
 * yaml_account_manager.h - YAML-based account manager
 *
 * Stores one YAML file per account in a Users/ directory.
 */

#ifndef MOBIUS_YAML_ACCOUNT_MANAGER_H
#define MOBIUS_YAML_ACCOUNT_MANAGER_H

#include "hotline/client_conn.h"  /* for hl_account_t, hl_account_mgr_t */

/*
 * mobius_yaml_account_mgr_new - Create a YAML-backed account manager.
 *
 * account_dir is the path to the Users/ directory.
 * Loads all .yaml files into memory on creation.
 * Returns NULL on failure.
 */
hl_account_mgr_t *mobius_yaml_account_mgr_new(const char *account_dir);

void mobius_yaml_account_mgr_free(hl_account_mgr_t *mgr);

#endif /* MOBIUS_YAML_ACCOUNT_MANAGER_H */
