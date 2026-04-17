/*
 * field.h - Hotline protocol field type
 *
 * Maps to: hotline/field.go
 *
 * A Field is a typed key-value pair within a Transaction.
 * Wire format: Type(2) + FieldSize(2) + Data(variable)
 */

#ifndef HOTLINE_FIELD_H
#define HOTLINE_FIELD_H

#include "hotline/types.h"
#include <string.h>

#define HL_MIN_FIELD_LEN 4

typedef struct {
    hl_field_type_t type;       /* Go: Type FieldType       */
    uint8_t         field_size[2]; /* Go: FieldSize [2]byte */
    uint8_t        *data;       /* Go: Data []byte          */
    uint16_t        data_len;   /* Convenience: decoded field_size */
} hl_field_t;

/*
 * hl_field_new - Create a new field with type and data.
 * Maps to: Go NewField()
 * Data is copied (caller retains ownership of src).
 * Returns 0 on success, -1 on allocation failure.
 */
int hl_field_new(hl_field_t *f, const hl_field_type_t type,
                 const uint8_t *data, uint16_t data_len);

/*
 * hl_field_deserialize - Parse a field from wire bytes.
 * Maps to: Go Field.Write() (confusing name in Go — it's deserialization)
 * Reads from buf[0..buf_len). Populates f. Data is copied.
 * Returns number of bytes consumed, or -1 on error.
 */
int hl_field_deserialize(hl_field_t *f, const uint8_t *buf, size_t buf_len);

/*
 * hl_field_serialize - Write a field to wire bytes.
 * Maps to: Go Field.Read() (confusing name in Go — it's serialization)
 * Writes to buf[0..buf_len). Returns bytes written, or -1 if buf too small.
 */
int hl_field_serialize(const hl_field_t *f, uint8_t *buf, size_t buf_len);

/*
 * hl_field_wire_size - Total wire size of this field (4 + data_len).
 */
static inline size_t hl_field_wire_size(const hl_field_t *f)
{
    return HL_MIN_FIELD_LEN + f->data_len;
}

/*
 * hl_field_scan - Scan for a complete field in a byte buffer.
 * Maps to: Go FieldScanner (bufio.SplitFunc)
 *
 * Given buf[0..buf_len), returns the total size of the next complete field
 * token, or 0 if more data is needed, or -1 on error.
 */
int hl_field_scan(const uint8_t *buf, size_t buf_len);

/*
 * hl_field_decode_int - Decode field data as a big-endian integer.
 * Maps to: Go Field.DecodeInt()
 * Handles both 2-byte and 4-byte variants (Frogblast/Heildrun compat).
 * Returns the decoded value, or -1 on error.
 */
int hl_field_decode_int(const hl_field_t *f);

/*
 * hl_field_decode_obfuscated_string - Decode a 255-rotated password string.
 * Maps to: Go Field.DecodeObfuscatedString() -> EncodeString()
 * Result is written to out, clamped to out_max bytes (including NUL).
 */
void hl_field_decode_obfuscated_string(const hl_field_t *f, char *out, size_t out_max);

/*
 * hl_field_free - Free allocated field data.
 */
void hl_field_free(hl_field_t *f);

#endif /* HOTLINE_FIELD_H */
