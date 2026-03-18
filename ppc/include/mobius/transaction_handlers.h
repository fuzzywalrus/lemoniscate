/*
 * transaction_handlers.h - All 43 transaction handler declarations
 *
 * Maps to: internal/mobius/transaction_handlers.go
 */

#ifndef MOBIUS_TRANSACTION_HANDLERS_H
#define MOBIUS_TRANSACTION_HANDLERS_H

#include "hotline/server.h"

/*
 * mobius_register_handlers - Register all 43 transaction handlers.
 * Maps to: Go RegisterHandlers()
 */
void mobius_register_handlers(hl_server_t *srv);

#endif /* MOBIUS_TRANSACTION_HANDLERS_H */
