/*
 * handlers.h - Transaction handler function type
 */

#ifndef HOTLINE_HANDLERS_H
#define HOTLINE_HANDLERS_H

#include "hotline/transaction.h"

/* Forward declaration */
#ifndef HL_CLIENT_CONN_TYPEDEF
#define HL_CLIENT_CONN_TYPEDEF
typedef struct hl_client_conn hl_client_conn_t;
#endif

/*
 * Handler function signature.
 *
 * Returns a malloc'd array of response transactions via *out_transactions.
 * *out_count is set to the number of responses.
 * Returns 0 on success, -1 on error.
 * Caller must free the transactions (hl_transaction_free each, then free the array).
 */
typedef int (*hl_handler_func_t)(hl_client_conn_t *cc,
                                 const hl_transaction_t *request,
                                 hl_transaction_t **out_transactions,
                                 int *out_count);

/* Max transaction type code for handler lookup table */
#define HL_HANDLER_TABLE_SIZE 512

#endif /* HOTLINE_HANDLERS_H */
