/*
 * platform_crypto.h - Platform-abstracted cryptographic hash API
 *
 * Provides SHA-1, SHA-256, and MD5 hash primitives used by HOPE
 * authentication, HKDF key derivation, and password hashing.
 *
 * macOS: CommonCrypto backend (crypto_commoncrypto.c)
 * Linux: OpenSSL EVP backend (crypto_openssl.c)
 */

#ifndef HOTLINE_PLATFORM_CRYPTO_H
#define HOTLINE_PLATFORM_CRYPTO_H

#include <stddef.h>
#include <stdint.h>

/* Digest sizes */
#define HL_SHA1_DIGEST_LENGTH    20
#define HL_SHA256_DIGEST_LENGTH  32
#define HL_MD5_DIGEST_LENGTH     16

/* Opaque hash context types */
typedef struct hl_sha1_ctx   hl_sha1_ctx_t;
typedef struct hl_sha256_ctx hl_sha256_ctx_t;
typedef struct hl_md5_ctx    hl_md5_ctx_t;

/* --- SHA-1 --- */

/* Allocate and initialize a SHA-1 context. Returns NULL on failure. */
hl_sha1_ctx_t *hl_sha1_init(void);

/* Feed data into the SHA-1 context. */
void hl_sha1_update(hl_sha1_ctx_t *ctx, const void *data, size_t len);

/* Finalize and write the 20-byte digest. Frees the context. */
void hl_sha1_final(hl_sha1_ctx_t *ctx, uint8_t out[HL_SHA1_DIGEST_LENGTH]);

/*
 * hl_sha1 - One-shot SHA-1 hash.
 * Hashes data and writes 20-byte digest to out.
 */
void hl_sha1(const void *data, size_t len,
             uint8_t out[HL_SHA1_DIGEST_LENGTH]);

/* --- SHA-256 --- */

/* Allocate and initialize a SHA-256 context. Returns NULL on failure. */
hl_sha256_ctx_t *hl_sha256_init(void);

/* Feed data into the SHA-256 context. */
void hl_sha256_update(hl_sha256_ctx_t *ctx, const void *data, size_t len);

/* Finalize and write the 32-byte digest. Frees the context. */
void hl_sha256_final(hl_sha256_ctx_t *ctx, uint8_t out[HL_SHA256_DIGEST_LENGTH]);

/*
 * hl_sha256 - One-shot SHA-256 hash.
 * Hashes data and writes 32-byte digest to out.
 */
void hl_sha256(const void *data, size_t len,
               uint8_t out[HL_SHA256_DIGEST_LENGTH]);

/* --- MD5 --- */

/* Allocate and initialize a MD5 context. Returns NULL on failure. */
hl_md5_ctx_t *hl_md5_init(void);

/* Feed data into the MD5 context. */
void hl_md5_update(hl_md5_ctx_t *ctx, const void *data, size_t len);

/* Finalize and write the 16-byte digest. Frees the context. */
void hl_md5_final(hl_md5_ctx_t *ctx, uint8_t out[HL_MD5_DIGEST_LENGTH]);

/*
 * hl_md5 - One-shot MD5 hash.
 * Hashes data and writes 16-byte digest to out.
 */
void hl_md5(const void *data, size_t len,
            uint8_t out[HL_MD5_DIGEST_LENGTH]);

#endif /* HOTLINE_PLATFORM_CRYPTO_H */
