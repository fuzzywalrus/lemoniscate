/*
 * user.c - Hotline protocol user implementation
 *
 * Maps to: hotline/user.go
 */

#include "hotline/user.h"
#include <string.h>

int hl_user_serialize(const hl_user_t *u, uint8_t *buf, size_t buf_len)
{
    /* Maps to: Go User.Read()
     * Wire format: ID(2) + Icon(2) + Flags(2) + NameLen(2) + Name(variable) */
    size_t needed = 8 + u->name_len;
    if (buf_len < needed) return -1;

    memcpy(buf,     u->id,    2);
    memcpy(buf + 2, u->icon,  2);
    memcpy(buf + 4, u->flags, 2);
    hl_write_u16(buf + 6, u->name_len);
    if (u->name_len > 0) {
        memcpy(buf + 8, u->name, u->name_len);
    }

    return (int)needed;
}

int hl_user_deserialize(hl_user_t *u, const uint8_t *buf, size_t buf_len)
{
    /* Maps to: Go User.Write() */
    if (buf_len < 8) return -1;

    uint16_t name_len = hl_read_u16(buf + 6);
    if (buf_len < (size_t)(8 + name_len)) return -1;
    if (name_len > HL_USER_NAME_MAX) return -1;

    memcpy(u->id,    buf,     2);
    memcpy(u->icon,  buf + 2, 2);
    memcpy(u->flags, buf + 4, 2);
    u->name_len = name_len;
    memcpy(u->name, buf + 8, name_len);
    u->name[name_len] = '\0';

    return 8 + name_len;
}

void hl_encode_string(const uint8_t *in, uint8_t *out, size_t len)
{
    /* Maps to: Go EncodeString()
     * The Hotline protocol uses 255-rotation for password "obfuscation".
     * Not secure, but it was the 90s! */
    size_t i;
    for (i = 0; i < len; i++) {
        out[i] = 255 - in[i];
    }
}
