/*
 * crypto_commoncrypto.c - macOS crypto backend using CommonCrypto
 *
 * Implements platform_crypto.h API for SHA-1 and MD5 hashing.
 * This file is only compiled on macOS (Darwin).
 */

#include "hotline/platform/platform_crypto.h"
#include <CommonCrypto/CommonDigest.h>
#include <stdlib.h>

/* --- SHA-1 --- */

struct hl_sha1_ctx {
    CC_SHA1_CTX cc;
};

hl_sha1_ctx_t *hl_sha1_init(void)
{
    hl_sha1_ctx_t *ctx = malloc(sizeof(*ctx));
    if (!ctx) return NULL;
    CC_SHA1_Init(&ctx->cc);
    return ctx;
}

void hl_sha1_update(hl_sha1_ctx_t *ctx, const void *data, size_t len)
{
    CC_SHA1_Update(&ctx->cc, data, (CC_LONG)len);
}

void hl_sha1_final(hl_sha1_ctx_t *ctx, uint8_t out[HL_SHA1_DIGEST_LENGTH])
{
    CC_SHA1_Final(out, &ctx->cc);
    free(ctx);
}

void hl_sha1(const void *data, size_t len, uint8_t out[HL_SHA1_DIGEST_LENGTH])
{
    CC_SHA1_CTX ctx;
    CC_SHA1_Init(&ctx);
    CC_SHA1_Update(&ctx, data, (CC_LONG)len);
    CC_SHA1_Final(out, &ctx);
}

/* --- MD5 --- */

struct hl_md5_ctx {
    CC_MD5_CTX cc;
};

hl_md5_ctx_t *hl_md5_init(void)
{
    hl_md5_ctx_t *ctx = malloc(sizeof(*ctx));
    if (!ctx) return NULL;
    CC_MD5_Init(&ctx->cc);
    return ctx;
}

void hl_md5_update(hl_md5_ctx_t *ctx, const void *data, size_t len)
{
    CC_MD5_Update(&ctx->cc, data, (CC_LONG)len);
}

void hl_md5_final(hl_md5_ctx_t *ctx, uint8_t out[HL_MD5_DIGEST_LENGTH])
{
    CC_MD5_Final(out, &ctx->cc);
    free(ctx);
}

void hl_md5(const void *data, size_t len, uint8_t out[HL_MD5_DIGEST_LENGTH])
{
    CC_MD5_CTX ctx;
    CC_MD5_Init(&ctx);
    CC_MD5_Update(&ctx, data, (CC_LONG)len);
    CC_MD5_Final(out, &ctx);
}
