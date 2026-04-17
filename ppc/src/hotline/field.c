/*
 * field.c - Hotline protocol field implementation
 *
 * Maps to: hotline/field.go
 */

#include "hotline/field.h"
#include <stdlib.h>
#include <string.h>

int hl_field_new(hl_field_t *f, const hl_field_type_t type,
                 const uint8_t *data, uint16_t data_len)
{
    memcpy(f->type, type, 2);
    hl_write_u16(f->field_size, data_len);
    f->data_len = data_len;

    if (data_len > 0) {
        f->data = (uint8_t *)malloc(data_len);
        if (!f->data) return -1;
        memcpy(f->data, data, data_len);
    } else {
        f->data = NULL;
    }

    return 0;
}

int hl_field_deserialize(hl_field_t *f, const uint8_t *buf, size_t buf_len)
{
    if (buf_len < HL_MIN_FIELD_LEN) return -1;

    memcpy(f->type, buf, 2);
    memcpy(f->field_size, buf + 2, 2);

    uint16_t data_size = hl_read_u16(f->field_size);
    if (buf_len < (size_t)(HL_MIN_FIELD_LEN + data_size)) return -1;

    f->data_len = data_size;
    if (data_size > 0) {
        f->data = (uint8_t *)malloc(data_size);
        if (!f->data) return -1;
        memcpy(f->data, buf + 4, data_size);
    } else {
        f->data = NULL;
    }

    return HL_MIN_FIELD_LEN + data_size;
}

int hl_field_serialize(const hl_field_t *f, uint8_t *buf, size_t buf_len)
{
    size_t needed = hl_field_wire_size(f);
    if (buf_len < needed) return -1;

    memcpy(buf, f->type, 2);
    memcpy(buf + 2, f->field_size, 2);
    if (f->data_len > 0 && f->data) {
        memcpy(buf + 4, f->data, f->data_len);
    }

    return (int)needed;
}

int hl_field_scan(const uint8_t *buf, size_t buf_len)
{
    /* Maps to: Go FieldScanner */
    if (buf_len < HL_MIN_FIELD_LEN) return 0; /* need more data */

    uint16_t data_size = hl_read_u16(buf + 2);
    size_t needed = HL_MIN_FIELD_LEN + data_size;

    if (needed > buf_len) return 0; /* need more data */

    return (int)needed;
}

int hl_field_decode_int(const hl_field_t *f)
{
    /* Maps to: Go Field.DecodeInt()
     * Official clients send uint32 as 2 bytes when possible,
     * but Frogblast/Heildrun always send 4 bytes. */
    if (f->data_len == 2) {
        return (int)hl_read_u16(f->data);
    } else if (f->data_len == 4) {
        return (int)hl_read_u32(f->data);
    }
    return -1;
}

void hl_field_decode_obfuscated_string(const hl_field_t *f, char *out, size_t out_max)
{
    /* Maps to: Go EncodeString() (255-rotation) used by DecodeObfuscatedString */
    if (out_max == 0) return;
    uint16_t len = f->data_len < (uint16_t)(out_max - 1)
                 ? f->data_len : (uint16_t)(out_max - 1);
    uint16_t i;
    for (i = 0; i < len; i++) {
        out[i] = (char)(255 - f->data[i]);
    }
    out[len] = '\0';
}

void hl_field_free(hl_field_t *f)
{
    if (f->data) {
        free(f->data);
        f->data = NULL;
    }
    f->data_len = 0;
}
