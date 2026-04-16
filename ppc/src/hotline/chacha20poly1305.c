/*
 * chacha20poly1305.c - Portable ChaCha20-Poly1305 AEAD (RFC 8439)
 *
 * Pure C implementation. No external crypto library dependencies.
 * Compatible with C89/C99 for Tiger (10.4 PPC) through modern targets.
 *
 * References:
 *   RFC 8439 - ChaCha20 and Poly1305 for IETF Protocols
 *   RFC 7539 - (predecessor, same algorithms)
 */

#include "hotline/chacha20poly1305.h"
#include <string.h>

/* ====================================================================
 * ChaCha20 (RFC 8439 Section 2.1-2.4)
 * ==================================================================== */

/* 32-bit left rotate */
static uint32_t rotl32(uint32_t v, int n)
{
    return (v << n) | (v >> (32 - n));
}

/* Quarter round: operates on four 32-bit words */
static void quarter_round(uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d)
{
    *a += *b; *d ^= *a; *d = rotl32(*d, 16);
    *c += *d; *b ^= *c; *b = rotl32(*b, 12);
    *a += *b; *d ^= *a; *d = rotl32(*d, 8);
    *c += *d; *b ^= *c; *b = rotl32(*b, 7);
}

/* Read a 32-bit little-endian word from bytes */
static uint32_t load32_le(const uint8_t *p)
{
    return (uint32_t)p[0]
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

/* Write a 32-bit little-endian word to bytes */
static void store32_le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v);
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

/* Write a 64-bit little-endian word to bytes */
static void store64_le(uint8_t *p, uint64_t v)
{
    int i;
    for (i = 0; i < 8; i++) {
        p[i] = (uint8_t)(v >> (8 * i));
    }
}

/*
 * ChaCha20 block function.
 * Generates a 64-byte keystream block from key, nonce, and block counter.
 *
 * State layout (RFC 8439 Section 2.3):
 *   cccccccc  cccccccc  cccccccc  cccccccc
 *   kkkkkkkk  kkkkkkkk  kkkkkkkk  kkkkkkkk
 *   kkkkkkkk  kkkkkkkk  kkkkkkkk  kkkkkkkk
 *   bbbbbbbb  nnnnnnnn  nnnnnnnn  nnnnnnnn
 *
 * c = constant ("expand 32-byte k")
 * k = key (256 bits)
 * b = block counter (32 bits)
 * n = nonce (96 bits)
 */
static void chacha20_block(const uint8_t key[32], const uint8_t nonce[12],
                           uint32_t counter, uint8_t out[64])
{
    uint32_t state[16];
    uint32_t working[16];
    int i;

    /* Initialize state */
    state[0]  = 0x61707865; /* "expa" */
    state[1]  = 0x3320646e; /* "nd 3" */
    state[2]  = 0x79622d32; /* "2-by" */
    state[3]  = 0x6b206574; /* "te k" */

    for (i = 0; i < 8; i++)
        state[4 + i] = load32_le(key + 4 * i);

    state[12] = counter;
    state[13] = load32_le(nonce);
    state[14] = load32_le(nonce + 4);
    state[15] = load32_le(nonce + 8);

    /* Copy state to working state */
    memcpy(working, state, sizeof(state));

    /* 20 rounds (10 double rounds) */
    for (i = 0; i < 10; i++) {
        /* Column rounds */
        quarter_round(&working[0], &working[4], &working[8],  &working[12]);
        quarter_round(&working[1], &working[5], &working[9],  &working[13]);
        quarter_round(&working[2], &working[6], &working[10], &working[14]);
        quarter_round(&working[3], &working[7], &working[11], &working[15]);
        /* Diagonal rounds */
        quarter_round(&working[0], &working[5], &working[10], &working[15]);
        quarter_round(&working[1], &working[6], &working[11], &working[12]);
        quarter_round(&working[2], &working[7], &working[8],  &working[13]);
        quarter_round(&working[3], &working[4], &working[9],  &working[14]);
    }

    /* Add original state and serialize */
    for (i = 0; i < 16; i++)
        store32_le(out + 4 * i, working[i] + state[i]);
}

/*
 * ChaCha20 encrypt/decrypt (XOR with keystream).
 * initial_counter is 1 for message encryption, 0 for Poly1305 key generation.
 */
static void chacha20_xor(const uint8_t key[32], const uint8_t nonce[12],
                         uint32_t initial_counter,
                         const uint8_t *in, uint8_t *out, size_t len)
{
    uint8_t block[64];
    uint32_t counter = initial_counter;
    size_t off;

    for (off = 0; off < len; off += 64) {
        size_t chunk = len - off;
        size_t j;
        if (chunk > 64) chunk = 64;

        chacha20_block(key, nonce, counter, block);
        counter++;

        for (j = 0; j < chunk; j++)
            out[off + j] = in[off + j] ^ block[j];
    }

    /* Zero keystream block */
    memset(block, 0, sizeof(block));
}

/* ====================================================================
 * Poly1305 (RFC 8439 Section 2.5)
 *
 * One-time authenticator using 130-bit arithmetic.
 * Implemented with 5x 26-bit limbs to avoid 128-bit integers.
 * ==================================================================== */

typedef struct {
    uint32_t r[5];     /* clamped r in 26-bit limbs */
    uint32_t h[5];     /* accumulator in 26-bit limbs */
    uint32_t pad[4];   /* s = key[16..31] as four 32-bit LE words */
} poly1305_state;

static void poly1305_init(poly1305_state *st, const uint8_t key[32])
{
    uint32_t t0, t1, t2, t3;

    /* r = key[0..15], clamped per spec */
    t0 = load32_le(key);
    t1 = load32_le(key + 4);
    t2 = load32_le(key + 8);
    t3 = load32_le(key + 12);

    st->r[0] = t0 & 0x3ffffff;
    st->r[1] = ((t0 >> 26) | (t1 << 6)) & 0x3ffff03;
    st->r[2] = ((t1 >> 20) | (t2 << 12)) & 0x3ffc0ff;
    st->r[3] = ((t2 >> 14) | (t3 << 18)) & 0x3f03fff;
    st->r[4] = (t3 >> 8) & 0x00fffff;

    /* h = 0 */
    st->h[0] = 0;
    st->h[1] = 0;
    st->h[2] = 0;
    st->h[3] = 0;
    st->h[4] = 0;

    /* s = key[16..31] */
    st->pad[0] = load32_le(key + 16);
    st->pad[1] = load32_le(key + 20);
    st->pad[2] = load32_le(key + 24);
    st->pad[3] = load32_le(key + 28);
}

static void poly1305_blocks(poly1305_state *st, const uint8_t *data,
                            size_t len, int is_final_block)
{
    uint32_t hibit = is_final_block ? 0 : (1 << 24); /* 2^128 bit */
    uint32_t r0 = st->r[0], r1 = st->r[1], r2 = st->r[2];
    uint32_t r3 = st->r[3], r4 = st->r[4];
    uint32_t s1 = r1 * 5, s2 = r2 * 5, s3 = r3 * 5, s4 = r4 * 5;
    uint32_t h0 = st->h[0], h1 = st->h[1], h2 = st->h[2];
    uint32_t h3 = st->h[3], h4 = st->h[4];

    while (len >= 16) {
        uint64_t d0, d1, d2, d3, d4;
        uint32_t c;

        /* h += m[i] */
        h0 += load32_le(data) & 0x3ffffff;
        h1 += (load32_le(data + 3) >> 2) & 0x3ffffff;
        h2 += (load32_le(data + 6) >> 4) & 0x3ffffff;
        h3 += (load32_le(data + 9) >> 6) & 0x3ffffff;
        h4 += (load32_le(data + 12) >> 8) | hibit;

        /* h *= r (mod 2^130 - 5) */
        d0 = ((uint64_t)h0 * r0) + ((uint64_t)h1 * s4) +
             ((uint64_t)h2 * s3) + ((uint64_t)h3 * s2) + ((uint64_t)h4 * s1);
        d1 = ((uint64_t)h0 * r1) + ((uint64_t)h1 * r0) +
             ((uint64_t)h2 * s4) + ((uint64_t)h3 * s3) + ((uint64_t)h4 * s2);
        d2 = ((uint64_t)h0 * r2) + ((uint64_t)h1 * r1) +
             ((uint64_t)h2 * r0) + ((uint64_t)h3 * s4) + ((uint64_t)h4 * s3);
        d3 = ((uint64_t)h0 * r3) + ((uint64_t)h1 * r2) +
             ((uint64_t)h2 * r1) + ((uint64_t)h3 * r0) + ((uint64_t)h4 * s4);
        d4 = ((uint64_t)h0 * r4) + ((uint64_t)h1 * r3) +
             ((uint64_t)h2 * r2) + ((uint64_t)h3 * r1) + ((uint64_t)h4 * r0);

        /* Partial reduction mod 2^130 - 5 */
        c = (uint32_t)(d0 >> 26); h0 = (uint32_t)d0 & 0x3ffffff;
        d1 += c; c = (uint32_t)(d1 >> 26); h1 = (uint32_t)d1 & 0x3ffffff;
        d2 += c; c = (uint32_t)(d2 >> 26); h2 = (uint32_t)d2 & 0x3ffffff;
        d3 += c; c = (uint32_t)(d3 >> 26); h3 = (uint32_t)d3 & 0x3ffffff;
        d4 += c; c = (uint32_t)(d4 >> 26); h4 = (uint32_t)d4 & 0x3ffffff;
        h0 += c * 5; c = h0 >> 26; h0 &= 0x3ffffff;
        h1 += c;

        data += 16;
        len -= 16;
    }

    st->h[0] = h0;
    st->h[1] = h1;
    st->h[2] = h2;
    st->h[3] = h3;
    st->h[4] = h4;
}

static void poly1305_final(poly1305_state *st, uint8_t tag[16])
{
    uint32_t h0, h1, h2, h3, h4, c;
    uint32_t g0, g1, g2, g3, g4;
    uint64_t f;
    uint32_t mask;

    h0 = st->h[0]; h1 = st->h[1]; h2 = st->h[2];
    h3 = st->h[3]; h4 = st->h[4];

    /* Full carry propagation */
    c = h1 >> 26; h1 &= 0x3ffffff;
    h2 += c; c = h2 >> 26; h2 &= 0x3ffffff;
    h3 += c; c = h3 >> 26; h3 &= 0x3ffffff;
    h4 += c; c = h4 >> 26; h4 &= 0x3ffffff;
    h0 += c * 5; c = h0 >> 26; h0 &= 0x3ffffff;
    h1 += c;

    /* Compute h + -(2^130 - 5) = h - p */
    g0 = h0 + 5; c = g0 >> 26; g0 &= 0x3ffffff;
    g1 = h1 + c; c = g1 >> 26; g1 &= 0x3ffffff;
    g2 = h2 + c; c = g2 >> 26; g2 &= 0x3ffffff;
    g3 = h3 + c; c = g3 >> 26; g3 &= 0x3ffffff;
    g4 = h4 + c - (1 << 26);

    /* Select h or g based on carry-out */
    mask = (g4 >> 31) - 1; /* all 1s if g4 >= 0 (h >= p), all 0s otherwise */
    g0 &= mask; g1 &= mask; g2 &= mask; g3 &= mask; g4 &= mask;
    mask = ~mask;
    h0 = (h0 & mask) | g0;
    h1 = (h1 & mask) | g1;
    h2 = (h2 & mask) | g2;
    h3 = (h3 & mask) | g3;
    h4 = (h4 & mask) | g4;

    /* Reassemble into 4 x 32-bit words */
    h0 = h0 | (h1 << 26);
    h1 = (h1 >> 6) | (h2 << 20);
    h2 = (h2 >> 12) | (h3 << 14);
    h3 = (h3 >> 18) | (h4 << 8);

    /* h = (h + pad) mod 2^128 */
    f = (uint64_t)h0 + st->pad[0]; h0 = (uint32_t)f;
    f = (uint64_t)h1 + st->pad[1] + (f >> 32); h1 = (uint32_t)f;
    f = (uint64_t)h2 + st->pad[2] + (f >> 32); h2 = (uint32_t)f;
    f = (uint64_t)h3 + st->pad[3] + (f >> 32); h3 = (uint32_t)f;

    store32_le(tag, h0);
    store32_le(tag + 4, h1);
    store32_le(tag + 8, h2);
    store32_le(tag + 12, h3);

    /* Zero sensitive state */
    memset(st, 0, sizeof(*st));
}

/* ====================================================================
 * AEAD Construction (RFC 8439 Section 2.8)
 *
 * 1. Generate one-time Poly1305 key from ChaCha20(key, nonce, counter=0)
 * 2. Encrypt plaintext with ChaCha20(key, nonce, counter=1)
 * 3. Build Poly1305 input: pad16(AAD) || pad16(ciphertext) || len(AAD) || len(CT)
 *    (For HOPE, AAD is empty)
 * 4. Compute tag = Poly1305(otk, poly_input)
 * ==================================================================== */

/*
 * Construct the Poly1305 AEAD data and compute tag.
 * aad_len is 0 for HOPE (no additional authenticated data).
 *
 * Feeds complete 16-byte blocks directly to poly1305_blocks (with hibit)
 * to avoid the partial-block finalization bug in poly1305_update.
 * The AEAD padding ensures the total input is always 16-byte aligned.
 */
static void aead_poly1305_tag(const uint8_t otk[32],
                              const uint8_t *aad, size_t aad_len,
                              const uint8_t *ciphertext, size_t ct_len,
                              uint8_t tag[16])
{
    poly1305_state st;
    uint8_t len_block[16];
    uint8_t zero_pad[16];
    size_t off;

    memset(zero_pad, 0, sizeof(zero_pad));

    poly1305_init(&st, otk);

    /* AAD: feed complete 16-byte blocks, then pad the tail to 16 bytes */
    if (aad_len > 0) {
        for (off = 0; off + 16 <= aad_len; off += 16)
            poly1305_blocks(&st, aad + off, 16, 0);
        if (off < aad_len) {
            /* Partial tail: copy into a padded 16-byte block */
            uint8_t padded[16];
            size_t tail = aad_len - off;
            memset(padded, 0, 16);
            memcpy(padded, aad + off, tail);
            poly1305_blocks(&st, padded, 16, 0);
        }
    }

    /* Ciphertext: feed complete 16-byte blocks, then pad the tail */
    if (ct_len > 0) {
        for (off = 0; off + 16 <= ct_len; off += 16)
            poly1305_blocks(&st, ciphertext + off, 16, 0);
        if (off < ct_len) {
            uint8_t padded[16];
            size_t tail = ct_len - off;
            memset(padded, 0, 16);
            memcpy(padded, ciphertext + off, tail);
            poly1305_blocks(&st, padded, 16, 0);
        }
    }

    /* Lengths: LE64(aad_len) || LE64(ct_len) — always exactly 16 bytes */
    store64_le(len_block, (uint64_t)aad_len);
    store64_le(len_block + 8, (uint64_t)ct_len);
    poly1305_blocks(&st, len_block, 16, 0);

    poly1305_final(&st, tag);
}

int hl_chacha20_poly1305_encrypt(
    const uint8_t key[HL_CHACHA20_KEY_SIZE],
    const uint8_t nonce[HL_CHACHA20_NONCE_SIZE],
    const uint8_t *plaintext, size_t plaintext_len,
    uint8_t *ciphertext_out,
    uint8_t tag_out[HL_POLY1305_TAG_SIZE])
{
    uint8_t otk_block[64];
    uint8_t otk[32];

    /* Step 1: Generate one-time Poly1305 key (counter=0, first 32 bytes) */
    chacha20_block(key, nonce, 0, otk_block);
    memcpy(otk, otk_block, 32);
    memset(otk_block, 0, sizeof(otk_block));

    /* Step 2: Encrypt plaintext (counter starts at 1) */
    if (plaintext_len > 0)
        chacha20_xor(key, nonce, 1, plaintext, ciphertext_out, plaintext_len);

    /* Step 3: Compute Poly1305 tag over ciphertext (no AAD) */
    aead_poly1305_tag(otk, NULL, 0, ciphertext_out, plaintext_len, tag_out);

    memset(otk, 0, sizeof(otk));
    return 0;
}

int hl_chacha20_poly1305_decrypt(
    const uint8_t key[HL_CHACHA20_KEY_SIZE],
    const uint8_t nonce[HL_CHACHA20_NONCE_SIZE],
    const uint8_t *ciphertext, size_t ciphertext_len,
    const uint8_t tag[HL_POLY1305_TAG_SIZE],
    uint8_t *plaintext_out)
{
    uint8_t otk_block[64];
    uint8_t otk[32];
    uint8_t computed_tag[16];
    int i;
    uint8_t diff;

    /* Step 1: Generate one-time Poly1305 key (counter=0, first 32 bytes) */
    chacha20_block(key, nonce, 0, otk_block);
    memcpy(otk, otk_block, 32);
    memset(otk_block, 0, sizeof(otk_block));

    /* Step 2: Compute expected tag over ciphertext (no AAD) */
    aead_poly1305_tag(otk, NULL, 0, ciphertext, ciphertext_len, computed_tag);
    memset(otk, 0, sizeof(otk));

    /* Step 3: Constant-time tag comparison */
    diff = 0;
    for (i = 0; i < 16; i++)
        diff |= computed_tag[i] ^ tag[i];

    if (diff != 0)
        return -1; /* Tag mismatch */

    /* Step 4: Decrypt ciphertext (counter starts at 1) */
    if (ciphertext_len > 0)
        chacha20_xor(key, nonce, 1, ciphertext, plaintext_out, ciphertext_len);

    return 0;
}
