/*
 * bonjour.h - Bonjour/mDNS service registration
 *
 * Maps to: oleksandr/bonjour usage in cmd/mobius-hotline-server/main.go
 *
 * Uses Tiger-native dns_sd.h (DNSServiceRegister).
 * Tiger ships mDNSResponder, so this requires no external dependencies.
 */

#ifndef HOTLINE_BONJOUR_H
#define HOTLINE_BONJOUR_H

/* Opaque handle for a registered Bonjour service */
typedef struct hl_bonjour_reg hl_bonjour_reg_t;

/*
 * hl_bonjour_register - Register a Hotline server with Bonjour.
 * Maps to: Go bonjour.Register(name, "_hotline._tcp", "", port, txtRecords, nil)
 *
 * service_name: Human-readable name (from config)
 * port: TCP port the server listens on
 *
 * Returns an opaque handle, or NULL on failure.
 * Call hl_bonjour_unregister() to clean up.
 */
hl_bonjour_reg_t *hl_bonjour_register(const char *service_name, int port);

/*
 * hl_bonjour_unregister - Remove Bonjour registration.
 */
void hl_bonjour_unregister(hl_bonjour_reg_t *reg);

#endif /* HOTLINE_BONJOUR_H */
