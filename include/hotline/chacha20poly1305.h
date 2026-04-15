/*
 * chacha20poly1305.h - Portable ChaCha20-Poly1305 AEAD (RFC 8439)
 *
 * Standalone C implementation with no external dependencies.
 * Compatible with C89/C99 for Tiger (10.4 PPC) through modern targets.
 *
 * Key: 32 bytes, Nonce: 12 bytes, Tag: 16 bytes.
 */

#ifndef HOTLINE_CHACHA20POLY1305_H
#define HOTLINE_CHACHA20POLY1305_H

#include <stddef.h>
#include <stdint.h>

#define HL_CHACHA20_KEY_SIZE    32
#define HL_CHACHA20_NONCE_SIZE  12
#define HL_POLY1305_TAG_SIZE    16

/*
 * hl_chacha20_poly1305_encrypt - AEAD seal (encrypt + authenticate)
 *
 * Encrypts `plaintext_len` bytes of plaintext using ChaCha20, then
 * computes a Poly1305 tag over the ciphertext (with no AAD).
 *
 * ciphertext_out must hold at least `plaintext_len` bytes.
 * tag_out must hold exactly 16 bytes.
 *
 * Returns 0 on success.
 */
int hl_chacha20_poly1305_encrypt(
    const uint8_t key[HL_CHACHA20_KEY_SIZE],
    const uint8_t nonce[HL_CHACHA20_NONCE_SIZE],
    const uint8_t *plaintext, size_t plaintext_len,
    uint8_t *ciphertext_out,
    uint8_t tag_out[HL_POLY1305_TAG_SIZE]);

/*
 * hl_chacha20_poly1305_decrypt - AEAD open (verify + decrypt)
 *
 * Verifies the Poly1305 tag, then decrypts `ciphertext_len` bytes.
 *
 * plaintext_out must hold at least `ciphertext_len` bytes.
 * Returns 0 on success, -1 if tag verification fails.
 */
int hl_chacha20_poly1305_decrypt(
    const uint8_t key[HL_CHACHA20_KEY_SIZE],
    const uint8_t nonce[HL_CHACHA20_NONCE_SIZE],
    const uint8_t *ciphertext, size_t ciphertext_len,
    const uint8_t tag[HL_POLY1305_TAG_SIZE],
    uint8_t *plaintext_out);

#endif /* HOTLINE_CHACHA20POLY1305_H */
