/*
 * crypto_openssl.c - Linux crypto backend using OpenSSL EVP APIs
 *
 * Implements platform_crypto.h API for SHA-1 and MD5 hashing.
 * Uses EVP_* APIs which are the recommended approach for OpenSSL 3.x.
 *
 * This file is only compiled on Linux.
 */

#include "hotline/platform/platform_crypto.h"

#ifdef __linux__

#include <openssl/evp.h>
#include <stdlib.h>
#include <string.h>

/* --- SHA-1 --- */

struct hl_sha1_ctx {
    EVP_MD_CTX *mdctx;
};

hl_sha1_ctx_t *hl_sha1_init(void)
{
    hl_sha1_ctx_t *ctx = malloc(sizeof(*ctx));
    if (!ctx) return NULL;

    ctx->mdctx = EVP_MD_CTX_new();
    if (!ctx->mdctx) {
        free(ctx);
        return NULL;
    }

    if (EVP_DigestInit_ex(ctx->mdctx, EVP_sha1(), NULL) != 1) {
        EVP_MD_CTX_free(ctx->mdctx);
        free(ctx);
        return NULL;
    }

    return ctx;
}

void hl_sha1_update(hl_sha1_ctx_t *ctx, const void *data, size_t len)
{
    EVP_DigestUpdate(ctx->mdctx, data, len);
}

void hl_sha1_final(hl_sha1_ctx_t *ctx, uint8_t out[HL_SHA1_DIGEST_LENGTH])
{
    unsigned int digest_len = 0;
    EVP_DigestFinal_ex(ctx->mdctx, out, &digest_len);
    EVP_MD_CTX_free(ctx->mdctx);
    free(ctx);
}

void hl_sha1(const void *data, size_t len, uint8_t out[HL_SHA1_DIGEST_LENGTH])
{
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx) return;
    EVP_DigestInit_ex(ctx, EVP_sha1(), NULL);
    EVP_DigestUpdate(ctx, data, len);
    unsigned int digest_len = 0;
    EVP_DigestFinal_ex(ctx, out, &digest_len);
    EVP_MD_CTX_free(ctx);
}

/* --- MD5 --- */

struct hl_md5_ctx {
    EVP_MD_CTX *mdctx;
};

hl_md5_ctx_t *hl_md5_init(void)
{
    hl_md5_ctx_t *ctx = malloc(sizeof(*ctx));
    if (!ctx) return NULL;

    ctx->mdctx = EVP_MD_CTX_new();
    if (!ctx->mdctx) {
        free(ctx);
        return NULL;
    }

    if (EVP_DigestInit_ex(ctx->mdctx, EVP_md5(), NULL) != 1) {
        EVP_MD_CTX_free(ctx->mdctx);
        free(ctx);
        return NULL;
    }

    return ctx;
}

void hl_md5_update(hl_md5_ctx_t *ctx, const void *data, size_t len)
{
    EVP_DigestUpdate(ctx->mdctx, data, len);
}

void hl_md5_final(hl_md5_ctx_t *ctx, uint8_t out[HL_MD5_DIGEST_LENGTH])
{
    unsigned int digest_len = 0;
    EVP_DigestFinal_ex(ctx->mdctx, out, &digest_len);
    EVP_MD_CTX_free(ctx->mdctx);
    free(ctx);
}

void hl_md5(const void *data, size_t len, uint8_t out[HL_MD5_DIGEST_LENGTH])
{
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx) return;
    EVP_DigestInit_ex(ctx, EVP_md5(), NULL);
    EVP_DigestUpdate(ctx, data, len);
    unsigned int digest_len = 0;
    EVP_DigestFinal_ex(ctx, out, &digest_len);
    EVP_MD_CTX_free(ctx);
}

#endif /* __linux__ */
