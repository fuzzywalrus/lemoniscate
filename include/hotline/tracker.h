/*
 * tracker.h - Tracker server registration
 *
 * Registers with Hotline tracker servers via UDP.
 */

#ifndef HOTLINE_TRACKER_H
#define HOTLINE_TRACKER_H

#include "hotline/types.h"

/*
 * hl_tracker_register - Send registration to a single tracker.
 *
 * tracker_addr: "host:port" format (default port 5499 if omitted)
 * server_port: this server's Hotline port
 * user_count: number of connected users
 * pass_id: 4-byte random server ID
 * name: server name
 * description: server description
 * password: tracker password (empty string if none)
 *
 * Returns 0 on success, -1 on error.
 */
int hl_tracker_register(const char *tracker_addr,
                        uint16_t server_port,
                        uint16_t user_count,
                        uint16_t tls_port,
                        const uint8_t pass_id[4],
                        const char *name,
                        const char *description,
                        const char *password);

/*
 * hl_tracker_register_all - Register with all configured trackers.
 * Parses "host:port" or "host:port:password" entries.
 *
 * Returns the number of trackers successfully registered.
 */
int hl_tracker_register_all(const char trackers[][256], int tracker_count,
                            uint16_t server_port,
                            uint16_t user_count,
                            uint16_t tls_port,
                            const uint8_t pass_id[4],
                            const char *name,
                            const char *description);

#endif /* HOTLINE_TRACKER_H */
