/*
 * password.c - Salted SHA-1 password hashing
 *
 * Uses platform crypto abstraction for SHA-1.
 * Format: "sha1:<8-byte hex salt>:<40-byte hex SHA-1>"
 *
 * Hash = SHA1(salt_bytes + password_bytes)
 */

#include "hotline/password.h"
#include "hotline/platform/platform_crypto.h"
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

/* Generate random bytes from /dev/urandom */
static int random_bytes(unsigned char *buf, size_t len)
{
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) return -1;
    size_t total = 0;
    while (total < len) {
        ssize_t r = read(fd, buf + total, len - total);
        if (r <= 0) { close(fd); return -1; }
        total += (size_t)r;
    }
    close(fd);
    return 0;
}

static void bytes_to_hex(const unsigned char *in, size_t in_len,
                          char *out, size_t out_len)
{
    size_t i;
    for (i = 0; i < in_len && (i * 2 + 2) < out_len; i++) {
        sprintf(out + i * 2, "%02x", in[i]);
    }
    out[i * 2] = '\0';
}

static int hex_to_bytes(const char *hex, unsigned char *out, size_t out_len)
{
    size_t hex_len = strlen(hex);
    size_t i;
    if (hex_len / 2 > out_len) return -1;
    for (i = 0; i < hex_len / 2; i++) {
        unsigned int byte;
        if (sscanf(hex + i * 2, "%2x", &byte) != 1) return -1;
        out[i] = (unsigned char)byte;
    }
    return (int)i;
}

int hl_password_hash(const char *password, char *out, size_t out_len)
{
    unsigned char salt[4]; /* 4 bytes = 8 hex chars */
    if (random_bytes(salt, sizeof(salt)) < 0) return -1;

    /* SHA1(salt + password) */
    hl_sha1_ctx_t *ctx = hl_sha1_init();
    unsigned char digest[HL_SHA1_DIGEST_LENGTH]; /* 20 bytes */
    if (!ctx) return -1;
    hl_sha1_update(ctx, salt, sizeof(salt));
    hl_sha1_update(ctx, password, strlen(password));
    hl_sha1_final(ctx, digest);

    /* Format: "sha1:<salt_hex>:<hash_hex>" */
    char salt_hex[9];
    char hash_hex[41];
    bytes_to_hex(salt, sizeof(salt), salt_hex, sizeof(salt_hex));
    bytes_to_hex(digest, HL_SHA1_DIGEST_LENGTH, hash_hex, sizeof(hash_hex));

    int needed = snprintf(out, out_len, "sha1:%s:%s", salt_hex, hash_hex);
    if (needed < 0 || (size_t)needed >= out_len) return -1;

    return 0;
}

int hl_password_verify(const char *password, const char *stored)
{
    /* If stored doesn't start with "sha1:", fall back to plaintext compare
     * (supports migration from old unhashed accounts) */
    if (strncmp(stored, "sha1:", 5) != 0) {
        return strcmp(password, stored) == 0 ? 1 : 0;
    }

    /* Parse "sha1:<salt_hex>:<hash_hex>" */
    const char *salt_hex = stored + 5;
    const char *colon = strchr(salt_hex, ':');
    if (!colon) return 0;

    size_t salt_hex_len = (size_t)(colon - salt_hex);
    if (salt_hex_len > 8) return 0;

    char salt_str[9];
    memcpy(salt_str, salt_hex, salt_hex_len);
    salt_str[salt_hex_len] = '\0';

    const char *expected_hex = colon + 1;

    /* Decode salt */
    unsigned char salt[4];
    int salt_len = hex_to_bytes(salt_str, salt, sizeof(salt));
    if (salt_len <= 0) return 0;

    /* Compute SHA1(salt + password) */
    hl_sha1_ctx_t *ctx2 = hl_sha1_init();
    unsigned char digest[HL_SHA1_DIGEST_LENGTH];
    if (!ctx2) return 0;
    hl_sha1_update(ctx2, salt, (size_t)salt_len);
    hl_sha1_update(ctx2, password, strlen(password));
    hl_sha1_final(ctx2, digest);

    /* Compare hex */
    char computed_hex[41];
    bytes_to_hex(digest, HL_SHA1_DIGEST_LENGTH, computed_hex, sizeof(computed_hex));

    return strcmp(computed_hex, expected_hex) == 0 ? 1 : 0;
}
