/*
 * ban_file.h - YAML-based ban list manager
 *
 * Maps to: internal/mobius/ban.go (BanFile)
 */

#ifndef MOBIUS_BAN_FILE_H
#define MOBIUS_BAN_FILE_H

#include "hotline/client_conn.h" /* for hl_ban_mgr_t */

/*
 * mobius_ban_file_new - Create a YAML-backed ban manager.
 * Maps to: Go NewBanFile()
 */
hl_ban_mgr_t *mobius_ban_file_new(const char *filepath);

void mobius_ban_file_free(hl_ban_mgr_t *mgr);

#endif /* MOBIUS_BAN_FILE_H */
