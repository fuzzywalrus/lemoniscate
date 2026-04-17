/*
 * user.h - Hotline protocol user types
 *
 * Maps to: hotline/user.go
 *
 * User wire format: ID(2) + Icon(2) + Flags(2) + NameLen(2) + Name(variable)
 */

#ifndef HOTLINE_USER_H
#define HOTLINE_USER_H

#include "hotline/types.h"

/* User flag bits (from hotline/user.go) */
#define HL_USER_FLAG_AWAY          0  /* User is away */
#define HL_USER_FLAG_ADMIN         1  /* User is admin */
#define HL_USER_FLAG_REFUSE_PM     2  /* User refuses private messages */
#define HL_USER_FLAG_REFUSE_PCHAT  3  /* User refuses private chat */

/* User option bits (client preferences) */
#define HL_USER_OPT_REFUSE_PM      0
#define HL_USER_OPT_REFUSE_CHAT    1
#define HL_USER_OPT_AUTO_RESPONSE  2

#define HL_USER_NAME_MAX 128

typedef struct {
    uint8_t  id[2];                    /* Go: ID    [2]byte */
    uint8_t  icon[2];                  /* Go: Icon  []byte (size 2) */
    hl_user_flags_t flags;             /* Go: Flags []byte (size 2) */
    char     name[HL_USER_NAME_MAX+1]; /* Go: Name  string */
    uint16_t name_len;                 /* Length of name */
} hl_user_t;

/*
 * hl_user_flags_is_set - Check if a flag bit is set.
 * Maps to: Go UserFlags.IsSet()
 */
static inline int hl_user_flags_is_set(const hl_user_flags_t flags, int bit)
{
    uint16_t val = hl_read_u16(flags);
    return (val >> bit) & 1;
}

/*
 * hl_user_flags_set - Set or clear a flag bit.
 * Maps to: Go UserFlags.Set()
 */
static inline void hl_user_flags_set(hl_user_flags_t flags, int bit, int val)
{
    uint16_t v = hl_read_u16(flags);
    if (val) {
        v |= (uint16_t)(1 << bit);
    } else {
        v &= (uint16_t)~(1 << bit);
    }
    hl_write_u16(flags, v);
}

/*
 * hl_user_serialize - Write user to wire format.
 * Maps to: Go User.Read()
 * Returns bytes written, or -1 if buf too small.
 */
int hl_user_serialize(const hl_user_t *u, uint8_t *buf, size_t buf_len);

/*
 * hl_user_deserialize - Parse user from wire bytes.
 * Maps to: Go User.Write()
 * Returns bytes consumed, or -1 on error.
 */
int hl_user_deserialize(hl_user_t *u, const uint8_t *buf, size_t buf_len);

/*
 * hl_encode_string - 255-rotation obfuscation for passwords.
 * Maps to: Go EncodeString()
 * Writes to out (must be at least len bytes). Works in-place (out == in is ok).
 */
void hl_encode_string(const uint8_t *in, uint8_t *out, size_t len);

#endif /* HOTLINE_USER_H */
