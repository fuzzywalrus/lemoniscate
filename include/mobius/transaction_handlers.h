/*
 * transaction_handlers.h - All 43 transaction handler declarations
 */

#ifndef MOBIUS_TRANSACTION_HANDLERS_H
#define MOBIUS_TRANSACTION_HANDLERS_H

#include "hotline/server.h"

/*
 * mobius_register_handlers - Register all 43 transaction handlers.
 */
void mobius_register_handlers(hl_server_t *srv);

/*
 * hl_build_notify_change_user - Build a TranNotifyChangeUser (301) transaction
 * with individual fields (UserName, UserID, IconID, UserFlags).
 */
void hl_build_notify_change_user(hl_client_conn_t *cc, hl_transaction_t *notify);
void hl_build_notify_change_user_free(hl_transaction_t *notify);

#endif /* MOBIUS_TRANSACTION_HANDLERS_H */
