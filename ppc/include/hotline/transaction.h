/*
 * transaction.h - Hotline protocol transaction type
 *
 * Maps to: hotline/transaction.go
 *
 * Wire format (22 bytes header + variable fields):
 *   Flags(1) + IsReply(1) + Type(2) + ID(4) + ErrorCode(4) +
 *   TotalSize(4) + DataSize(4) + ParamCount(2) + Fields...
 */

#ifndef HOTLINE_TRANSACTION_H
#define HOTLINE_TRANSACTION_H

#include "hotline/types.h"
#include "hotline/field.h"

#define HL_TRAN_HEADER_LEN 22
#define HL_TRAN_MAX_FIELDS 128

typedef struct {
    uint8_t         flags;          /* Go: Flags      byte      - Reserved (0) */
    uint8_t         is_reply;       /* Go: IsReply    byte      - 0=request, 1=reply */
    hl_tran_type_t  type;           /* Go: Type       TranType  */
    uint8_t         id[4];          /* Go: ID         [4]byte   */
    uint8_t         error_code[4];  /* Go: ErrorCode  [4]byte   */
    uint8_t         total_size[4];  /* Go: TotalSize  [4]byte   */
    uint8_t         data_size[4];   /* Go: DataSize   [4]byte   */
    uint8_t         param_count[2]; /* Go: ParamCount [2]byte   */
    hl_field_t     *fields;         /* Go: Fields     []Field   */
    uint16_t        field_count;    /* Convenience: number of fields */

    hl_client_id_t  client_id;      /* Go: ClientID - internal routing */
} hl_transaction_t;

/*
 * hl_transaction_new - Create a new transaction with random ID.
 * Maps to: Go NewTransaction()
 * Fields are copied. Returns 0 on success, -1 on error.
 */
int hl_transaction_new(hl_transaction_t *t, const hl_tran_type_t type,
                       const hl_client_id_t client_id,
                       const hl_field_t *fields, uint16_t field_count);

/*
 * hl_transaction_deserialize - Parse a transaction from wire bytes.
 * Maps to: Go Transaction.Write()
 * buf must contain a complete transaction (use hl_transaction_scan first).
 * Returns bytes consumed, or -1 on error.
 */
int hl_transaction_deserialize(hl_transaction_t *t,
                               const uint8_t *buf, size_t buf_len);

/*
 * hl_transaction_serialize - Write a transaction to wire bytes.
 * Maps to: Go Transaction.Read()
 * Returns bytes written, or -1 if buf too small.
 * Use hl_transaction_wire_size() to pre-calculate needed buffer size.
 */
int hl_transaction_serialize(const hl_transaction_t *t,
                             uint8_t *buf, size_t buf_len);

/*
 * hl_transaction_wire_size - Calculate total wire size.
 * Maps to: 22 (header) + sum of field wire sizes
 */
size_t hl_transaction_wire_size(const hl_transaction_t *t);

/*
 * hl_transaction_scan - Scan for a complete transaction in a byte buffer.
 * Maps to: Go transactionScanner (bufio.SplitFunc)
 *
 * Returns the total size of the next complete transaction, or 0 if more
 * data is needed, or -1 on error.
 */
int hl_transaction_scan(const uint8_t *buf, size_t buf_len);

/*
 * hl_transaction_get_field - Find a field by type in a transaction.
 * Maps to: Go Transaction.GetField()
 * Returns pointer to the field, or NULL if not found.
 */
const hl_field_t *hl_transaction_get_field(const hl_transaction_t *t,
                                           const hl_field_type_t type);

/*
 * hl_transaction_payload_size - Calculate the payload size (fields + param count).
 * Maps to: Go Transaction.Size()
 * Returns the value that goes into TotalSize/DataSize header fields.
 */
uint32_t hl_transaction_payload_size(const hl_transaction_t *t);

/*
 * hl_transaction_free - Free allocated transaction fields.
 */
void hl_transaction_free(hl_transaction_t *t);

/*
 * hl_transaction_type_name - Human-readable name for a transaction type.
 * Maps to: Go tranTypeNames map
 * Returns a static string, or "Unknown" if not recognized.
 */
const char *hl_transaction_type_name(const hl_tran_type_t type);

#endif /* HOTLINE_TRANSACTION_H */
