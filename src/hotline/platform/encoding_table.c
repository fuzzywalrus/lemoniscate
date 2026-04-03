/*
 * encoding_table.c - MacRoman <-> UTF-8 via static lookup table
 *
 * Platform-independent implementation (no CoreFoundation dependency).
 * MacRoman is a fixed single-byte encoding — the first 128 code points
 * match ASCII, and the upper 128 map to specific Unicode code points.
 *
 * This file is compiled on Linux (and can optionally replace encoding_cf.c
 * on macOS for consistency).
 */

#include "hotline/encoding.h"
#include <string.h>

/*
 * MacRoman code points 0x80-0xFF mapped to Unicode code points.
 * Each entry is the UTF-8 encoding of the corresponding MacRoman byte.
 * Index 0 = MacRoman 0x80, index 127 = MacRoman 0xFF.
 */
static const char *macroman_to_utf8_table[128] = {
    /* 0x80 */ "\xC3\x84",     /* A-umlaut */
    /* 0x81 */ "\xC3\x85",     /* A-ring */
    /* 0x82 */ "\xC3\x87",     /* C-cedilla */
    /* 0x83 */ "\xC3\x89",     /* E-acute */
    /* 0x84 */ "\xC3\x91",     /* N-tilde */
    /* 0x85 */ "\xC3\x96",     /* O-umlaut */
    /* 0x86 */ "\xC3\x9C",     /* U-umlaut */
    /* 0x87 */ "\xC3\xA1",     /* a-acute */
    /* 0x88 */ "\xC3\xA0",     /* a-grave */
    /* 0x89 */ "\xC3\xA2",     /* a-circumflex */
    /* 0x8A */ "\xC3\xA4",     /* a-umlaut */
    /* 0x8B */ "\xC3\xA3",     /* a-tilde */
    /* 0x8C */ "\xC3\xA5",     /* a-ring */
    /* 0x8D */ "\xC3\xA7",     /* c-cedilla */
    /* 0x8E */ "\xC3\xA9",     /* e-acute */
    /* 0x8F */ "\xC3\xA8",     /* e-grave */
    /* 0x90 */ "\xC3\xAA",     /* e-circumflex */
    /* 0x91 */ "\xC3\xAB",     /* e-umlaut */
    /* 0x92 */ "\xC3\xAD",     /* i-acute */
    /* 0x93 */ "\xC3\xAC",     /* i-grave */
    /* 0x94 */ "\xC3\xAE",     /* i-circumflex */
    /* 0x95 */ "\xC3\xAF",     /* i-umlaut */
    /* 0x96 */ "\xC3\xB1",     /* n-tilde */
    /* 0x97 */ "\xC3\xB3",     /* o-acute */
    /* 0x98 */ "\xC3\xB2",     /* o-grave */
    /* 0x99 */ "\xC3\xB4",     /* o-circumflex */
    /* 0x9A */ "\xC3\xB6",     /* o-umlaut */
    /* 0x9B */ "\xC3\xB5",     /* o-tilde */
    /* 0x9C */ "\xC3\xBA",     /* u-acute */
    /* 0x9D */ "\xC3\xB9",     /* u-grave */
    /* 0x9E */ "\xC3\xBB",     /* u-circumflex */
    /* 0x9F */ "\xC3\xBC",     /* u-umlaut */
    /* 0xA0 */ "\xE2\x80\xA0", /* dagger */
    /* 0xA1 */ "\xC2\xB0",     /* degree */
    /* 0xA2 */ "\xC2\xA2",     /* cent */
    /* 0xA3 */ "\xC2\xA3",     /* pound */
    /* 0xA4 */ "\xC2\xA7",     /* section */
    /* 0xA5 */ "\xE2\x80\xA2", /* bullet */
    /* 0xA6 */ "\xC2\xB6",     /* pilcrow */
    /* 0xA7 */ "\xC3\x9F",     /* sharp-s */
    /* 0xA8 */ "\xC2\xAE",     /* registered */
    /* 0xA9 */ "\xC2\xA9",     /* copyright */
    /* 0xAA */ "\xE2\x84\xA2", /* trademark */
    /* 0xAB */ "\xC2\xB4",     /* acute accent */
    /* 0xAC */ "\xC2\xA8",     /* diaeresis */
    /* 0xAD */ "\xE2\x89\xA0", /* not-equal */
    /* 0xAE */ "\xC3\x86",     /* AE */
    /* 0xAF */ "\xC3\x98",     /* O-stroke */
    /* 0xB0 */ "\xE2\x88\x9E", /* infinity */
    /* 0xB1 */ "\xC2\xB1",     /* plus-minus */
    /* 0xB2 */ "\xE2\x89\xA4", /* less-equal */
    /* 0xB3 */ "\xE2\x89\xA5", /* greater-equal */
    /* 0xB4 */ "\xC2\xA5",     /* yen */
    /* 0xB5 */ "\xC2\xB5",     /* micro */
    /* 0xB6 */ "\xE2\x88\x82", /* partial diff */
    /* 0xB7 */ "\xE2\x88\x91", /* summation */
    /* 0xB8 */ "\xE2\x88\x8F", /* product */
    /* 0xB9 */ "\xCF\x80",     /* pi */
    /* 0xBA */ "\xE2\x88\xAB", /* integral */
    /* 0xBB */ "\xC2\xAA",     /* feminine ordinal */
    /* 0xBC */ "\xC2\xBA",     /* masculine ordinal */
    /* 0xBD */ "\xCE\xA9",     /* Omega */
    /* 0xBE */ "\xC3\xA6",     /* ae */
    /* 0xBF */ "\xC3\xB8",     /* o-stroke */
    /* 0xC0 */ "\xC2\xBF",     /* inv question */
    /* 0xC1 */ "\xC2\xA1",     /* inv exclamation */
    /* 0xC2 */ "\xC2\xAC",     /* not */
    /* 0xC3 */ "\xE2\x88\x9A", /* sqrt */
    /* 0xC4 */ "\xC6\x92",     /* f-hook */
    /* 0xC5 */ "\xE2\x89\x88", /* almost-equal */
    /* 0xC6 */ "\xE2\x88\x86", /* delta/increment */
    /* 0xC7 */ "\xC2\xAB",     /* left guillemet */
    /* 0xC8 */ "\xC2\xBB",     /* right guillemet */
    /* 0xC9 */ "\xE2\x80\xA6", /* ellipsis */
    /* 0xCA */ "\xC2\xA0",     /* nbsp */
    /* 0xCB */ "\xC3\x80",     /* A-grave */
    /* 0xCC */ "\xC3\x83",     /* A-tilde */
    /* 0xCD */ "\xC3\x95",     /* O-tilde */
    /* 0xCE */ "\xC5\x92",     /* OE */
    /* 0xCF */ "\xC5\x93",     /* oe */
    /* 0xD0 */ "\xE2\x80\x93", /* en-dash */
    /* 0xD1 */ "\xE2\x80\x94", /* em-dash */
    /* 0xD2 */ "\xE2\x80\x9C", /* left double quote */
    /* 0xD3 */ "\xE2\x80\x9D", /* right double quote */
    /* 0xD4 */ "\xE2\x80\x98", /* left single quote */
    /* 0xD5 */ "\xE2\x80\x99", /* right single quote */
    /* 0xD6 */ "\xC3\xB7",     /* division */
    /* 0xD7 */ "\xE2\x97\x8A", /* lozenge */
    /* 0xD8 */ "\xC3\xBF",     /* y-umlaut */
    /* 0xD9 */ "\xC5\xB8",     /* Y-umlaut */
    /* 0xDA */ "\xE2\x81\x84", /* fraction slash */
    /* 0xDB */ "\xE2\x82\xAC", /* Euro sign */
    /* 0xDC */ "\xE2\x80\xB9", /* left single guillemet */
    /* 0xDD */ "\xE2\x80\xBA", /* right single guillemet */
    /* 0xDE */ "\xEF\xAC\x81", /* fi ligature */
    /* 0xDF */ "\xEF\xAC\x82", /* fl ligature */
    /* 0xE0 */ "\xE2\x80\xA1", /* double dagger */
    /* 0xE1 */ "\xC2\xB7",     /* middle dot */
    /* 0xE2 */ "\xE2\x80\x9A", /* single low-9 quote */
    /* 0xE3 */ "\xE2\x80\x9E", /* double low-9 quote */
    /* 0xE4 */ "\xE2\x80\xB0", /* per mille */
    /* 0xE5 */ "\xC3\x82",     /* A-circumflex */
    /* 0xE6 */ "\xC3\x8A",     /* E-circumflex */
    /* 0xE7 */ "\xC3\x81",     /* A-acute */
    /* 0xE8 */ "\xC3\x8B",     /* E-umlaut */
    /* 0xE9 */ "\xC3\x88",     /* E-grave */
    /* 0xEA */ "\xC3\x8D",     /* I-acute */
    /* 0xEB */ "\xC3\x8E",     /* I-circumflex */
    /* 0xEC */ "\xC3\x8F",     /* I-umlaut */
    /* 0xED */ "\xC3\x8C",     /* I-grave */
    /* 0xEE */ "\xC3\x93",     /* O-acute */
    /* 0xEF */ "\xC3\x94",     /* O-circumflex */
    /* 0xF0 */ "\xEF\xA3\xBF", /* Apple logo (U+F8FF) */
    /* 0xF1 */ "\xC3\x92",     /* O-grave */
    /* 0xF2 */ "\xC3\x9A",     /* U-acute */
    /* 0xF3 */ "\xC3\x9B",     /* U-circumflex */
    /* 0xF4 */ "\xC3\x99",     /* U-grave */
    /* 0xF5 */ "\xC4\xB1",     /* dotless i */
    /* 0xF6 */ "\xCB\x86",     /* circumflex modifier */
    /* 0xF7 */ "\xCB\x9C",     /* tilde modifier */
    /* 0xF8 */ "\xC2\xAF",     /* macron */
    /* 0xF9 */ "\xCB\x98",     /* breve */
    /* 0xFA */ "\xCB\x99",     /* dot above */
    /* 0xFB */ "\xCB\x9A",     /* ring above */
    /* 0xFC */ "\xC2\xB8",     /* cedilla */
    /* 0xFD */ "\xCB\x9D",     /* double acute */
    /* 0xFE */ "\xCB\x9B",     /* ogonek */
    /* 0xFF */ "\xCB\x87",     /* caron */
};

int hl_macroman_to_utf8(const char *in, size_t in_len,
                        char *out, size_t out_len)
{
    if (out_len == 0) return -1;

    if (in_len == 0) {
        out[0] = '\0';
        return 0;
    }

    size_t pos = 0;
    size_t i;
    for (i = 0; i < in_len; i++) {
        unsigned char c = (unsigned char)in[i];
        if (c < 0x80) {
            /* ASCII — direct copy */
            if (pos + 1 >= out_len) break;
            out[pos++] = (char)c;
        } else {
            const char *utf8 = macroman_to_utf8_table[c - 0x80];
            size_t utf8_len = strlen(utf8);
            if (pos + utf8_len >= out_len) break;
            memcpy(out + pos, utf8, utf8_len);
            pos += utf8_len;
        }
    }

    out[pos] = '\0';
    return (int)pos;
}

/*
 * Reverse lookup: build a mapping from Unicode code points back to
 * MacRoman bytes. We use a simple linear search since the table is
 * only 128 entries and this function is not performance-critical.
 */

/* Decode a UTF-8 sequence starting at p, return the Unicode code point
 * and advance *consumed by the number of bytes read. Returns 0xFFFD on error. */
static unsigned int decode_utf8(const unsigned char *p, size_t remaining,
                                int *consumed)
{
    if (remaining == 0) { *consumed = 0; return 0xFFFD; }

    unsigned char c = p[0];
    if (c < 0x80) {
        *consumed = 1;
        return c;
    } else if ((c & 0xE0) == 0xC0 && remaining >= 2) {
        *consumed = 2;
        return ((unsigned int)(c & 0x1F) << 6) | (p[1] & 0x3F);
    } else if ((c & 0xF0) == 0xE0 && remaining >= 3) {
        *consumed = 3;
        return ((unsigned int)(c & 0x0F) << 12) |
               ((unsigned int)(p[1] & 0x3F) << 6) |
               (p[2] & 0x3F);
    } else if ((c & 0xF8) == 0xF0 && remaining >= 4) {
        *consumed = 4;
        return ((unsigned int)(c & 0x07) << 18) |
               ((unsigned int)(p[1] & 0x3F) << 12) |
               ((unsigned int)(p[2] & 0x3F) << 6) |
               (p[3] & 0x3F);
    }
    *consumed = 1;
    return 0xFFFD;
}

/* Decode the UTF-8 string in a table entry to its Unicode code point */
static unsigned int table_entry_codepoint(int idx)
{
    const char *s = macroman_to_utf8_table[idx];
    int consumed;
    return decode_utf8((const unsigned char *)s, strlen(s), &consumed);
}

/* Find MacRoman byte for a Unicode code point. Returns -1 if not found. */
static int unicode_to_macroman(unsigned int cp)
{
    /* ASCII range */
    if (cp < 0x80) return (int)cp;

    /* Search the upper 128 table */
    int i;
    for (i = 0; i < 128; i++) {
        if (table_entry_codepoint(i) == cp)
            return 0x80 + i;
    }
    return -1;
}

int hl_utf8_to_macroman(const char *in, size_t in_len,
                        char *out, size_t out_len)
{
    if (in_len == 0) return 0;

    size_t pos = 0;
    size_t i = 0;
    while (i < in_len && pos < out_len) {
        int consumed;
        unsigned int cp = decode_utf8((const unsigned char *)in + i,
                                      in_len - i, &consumed);
        int mr = unicode_to_macroman(cp);
        if (mr < 0) mr = '?'; /* lossy replacement */
        out[pos++] = (char)(unsigned char)mr;
        i += (size_t)consumed;
    }

    return (int)pos;
}
