/*
 * hope.c - HOPE (Hotline One-time Password Extension) implementation
 *
 * Server-side HOPE: MAC-based authentication and RC4 transport encryption.
 *
 * Uses platform crypto abstraction for SHA-1 and MD5 hash primitives.
 * RC4 is implemented manually for guaranteed Tiger compatibility.
 * HMAC is implemented manually using the standard HMAC construction.
 */

#include "hotline/hope.h"
#include "hotline/server.h"
#include "hotline/platform/platform_crypto.h"
#include "hotline/chacha20poly1305.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <arpa/inet.h>

/* --- Algorithm name table --- */

static const char *mac_alg_names[] = {
    "HMAC-SHA256", /* HL_HOPE_MAC_HMAC_SHA256 */
    "HMAC-SHA1",   /* HL_HOPE_MAC_HMAC_SHA1 */
    "SHA1",        /* HL_HOPE_MAC_SHA1 */
    "HMAC-MD5",    /* HL_HOPE_MAC_HMAC_MD5 */
    "MD5",         /* HL_HOPE_MAC_MD5 */
    "INVERSE"      /* HL_HOPE_MAC_INVERSE */
};

/* Server app identification */
#define HOPE_APP_ID     "LMSC"
#define HOPE_APP_STRING "Lemoniscate 0.1.7"

/* --- Random bytes (reuses pattern from password.c) --- */

static int random_bytes(uint8_t *buf, size_t len)
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

/* ====================================================================
 * RC4 (ARC4) stream cipher
 *
 * Manual implementation matching the Rust reference in hope_stream.rs.
 * ==================================================================== */

void hl_rc4_init(hl_rc4_t *rc4, const uint8_t *key, size_t key_len)
{
    int i;
    uint8_t j = 0;

    for (i = 0; i < 256; i++)
        rc4->s[i] = (uint8_t)i;

    for (i = 0; i < 256; i++) {
        j = (uint8_t)(j + rc4->s[i] + key[i % key_len]);
        /* swap */
        uint8_t tmp = rc4->s[i];
        rc4->s[i] = rc4->s[j];
        rc4->s[j] = tmp;
    }
    rc4->i = 0;
    rc4->j = 0;
}

void hl_rc4_process(hl_rc4_t *rc4, uint8_t *data, size_t len)
{
    size_t n;
    for (n = 0; n < len; n++) {
        rc4->i = (uint8_t)(rc4->i + 1);
        rc4->j = (uint8_t)(rc4->j + rc4->s[rc4->i]);
        /* swap */
        uint8_t tmp = rc4->s[rc4->i];
        rc4->s[rc4->i] = rc4->s[rc4->j];
        rc4->s[rc4->j] = tmp;
        uint8_t k = rc4->s[(uint8_t)(rc4->s[rc4->i] + rc4->s[rc4->j])];
        data[n] ^= k;
    }
}

/* ====================================================================
 * HMAC implementation using platform crypto hash primitives
 *
 * HMAC(K, m) = H((K' ^ opad) || H((K' ^ ipad) || m))
 * where K' = H(K) if len(K) > block_size, else K zero-padded
 * ipad = 0x36 repeated, opad = 0x5C repeated
 * ==================================================================== */

#define SHA256_BLOCK_SIZE  64
#define SHA256_DIGEST_SIZE HL_SHA256_DIGEST_LENGTH /* 32 */
#define SHA1_BLOCK_SIZE    64
#define SHA1_DIGEST_SIZE   HL_SHA1_DIGEST_LENGTH  /* 20 */
#define MD5_BLOCK_SIZE     64
#define MD5_DIGEST_SIZE    HL_MD5_DIGEST_LENGTH   /* 16 */

static void hmac_sha256(const uint8_t *key, size_t key_len,
                        const uint8_t *data, size_t data_len,
                        uint8_t *out)
{
    uint8_t k_prime[SHA256_BLOCK_SIZE];
    int i;

    memset(k_prime, 0, SHA256_BLOCK_SIZE);
    if (key_len > SHA256_BLOCK_SIZE) {
        hl_sha256(key, key_len, k_prime);
    } else {
        memcpy(k_prime, key, key_len);
    }

    /* Inner: H((K' ^ ipad) || data) */
    uint8_t inner_pad[SHA256_BLOCK_SIZE];
    for (i = 0; i < SHA256_BLOCK_SIZE; i++)
        inner_pad[i] = k_prime[i] ^ 0x36;

    hl_sha256_ctx_t *ctx = hl_sha256_init();
    hl_sha256_update(ctx, inner_pad, SHA256_BLOCK_SIZE);
    hl_sha256_update(ctx, data, data_len);
    uint8_t inner_hash[SHA256_DIGEST_SIZE];
    hl_sha256_final(ctx, inner_hash);

    /* Outer: H((K' ^ opad) || inner_hash) */
    uint8_t outer_pad[SHA256_BLOCK_SIZE];
    for (i = 0; i < SHA256_BLOCK_SIZE; i++)
        outer_pad[i] = k_prime[i] ^ 0x5C;

    ctx = hl_sha256_init();
    hl_sha256_update(ctx, outer_pad, SHA256_BLOCK_SIZE);
    hl_sha256_update(ctx, inner_hash, SHA256_DIGEST_SIZE);
    hl_sha256_final(ctx, out);
}

static void hmac_sha1(const uint8_t *key, size_t key_len,
                      const uint8_t *data, size_t data_len,
                      uint8_t *out)
{
    uint8_t k_prime[SHA1_BLOCK_SIZE];
    int i;

    memset(k_prime, 0, SHA1_BLOCK_SIZE);
    if (key_len > SHA1_BLOCK_SIZE) {
        hl_sha1(key, key_len, k_prime);
    } else {
        memcpy(k_prime, key, key_len);
    }

    /* Inner: H((K' ^ ipad) || data) */
    uint8_t inner_pad[SHA1_BLOCK_SIZE];
    for (i = 0; i < SHA1_BLOCK_SIZE; i++)
        inner_pad[i] = k_prime[i] ^ 0x36;

    hl_sha1_ctx_t *ctx = hl_sha1_init();
    hl_sha1_update(ctx, inner_pad, SHA1_BLOCK_SIZE);
    hl_sha1_update(ctx, data, data_len);
    uint8_t inner_hash[SHA1_DIGEST_SIZE];
    hl_sha1_final(ctx, inner_hash);

    /* Outer: H((K' ^ opad) || inner_hash) */
    uint8_t outer_pad[SHA1_BLOCK_SIZE];
    for (i = 0; i < SHA1_BLOCK_SIZE; i++)
        outer_pad[i] = k_prime[i] ^ 0x5C;

    ctx = hl_sha1_init();
    hl_sha1_update(ctx, outer_pad, SHA1_BLOCK_SIZE);
    hl_sha1_update(ctx, inner_hash, SHA1_DIGEST_SIZE);
    hl_sha1_final(ctx, out);
}

static void hmac_md5(const uint8_t *key, size_t key_len,
                     const uint8_t *data, size_t data_len,
                     uint8_t *out)
{
    uint8_t k_prime[MD5_BLOCK_SIZE];
    int i;

    memset(k_prime, 0, MD5_BLOCK_SIZE);
    if (key_len > MD5_BLOCK_SIZE) {
        hl_md5(key, key_len, k_prime);
    } else {
        memcpy(k_prime, key, key_len);
    }

    uint8_t inner_pad[MD5_BLOCK_SIZE];
    for (i = 0; i < MD5_BLOCK_SIZE; i++)
        inner_pad[i] = k_prime[i] ^ 0x36;

    hl_md5_ctx_t *ctx = hl_md5_init();
    hl_md5_update(ctx, inner_pad, MD5_BLOCK_SIZE);
    hl_md5_update(ctx, data, data_len);
    uint8_t inner_hash[MD5_DIGEST_SIZE];
    hl_md5_final(ctx, inner_hash);

    uint8_t outer_pad[MD5_BLOCK_SIZE];
    for (i = 0; i < MD5_BLOCK_SIZE; i++)
        outer_pad[i] = k_prime[i] ^ 0x5C;

    ctx = hl_md5_init();
    hl_md5_update(ctx, outer_pad, MD5_BLOCK_SIZE);
    hl_md5_update(ctx, inner_hash, MD5_DIGEST_SIZE);
    hl_md5_final(ctx, out);
}

/* ====================================================================
 * MAC dispatcher
 * ==================================================================== */

void hl_hope_mac(hl_hope_mac_alg_t alg,
                 const uint8_t *data, size_t data_len,
                 const uint8_t *key, size_t key_len,
                 uint8_t *out, size_t *out_len)
{
    switch (alg) {
    case HL_HOPE_MAC_HMAC_SHA256:
        hmac_sha256(key, key_len, data, data_len, out);
        *out_len = SHA256_DIGEST_SIZE;
        break;

    case HL_HOPE_MAC_HMAC_SHA1:
        hmac_sha1(key, key_len, data, data_len, out);
        *out_len = SHA1_DIGEST_SIZE;
        break;

    case HL_HOPE_MAC_SHA1: {
        /* SHA1(key || data) */
        hl_sha1_ctx_t *ctx = hl_sha1_init();
        hl_sha1_update(ctx, key, key_len);
        hl_sha1_update(ctx, data, data_len);
        hl_sha1_final(ctx, out);
        *out_len = SHA1_DIGEST_SIZE;
        break;
    }

    case HL_HOPE_MAC_HMAC_MD5:
        hmac_md5(key, key_len, data, data_len, out);
        *out_len = MD5_DIGEST_SIZE;
        break;

    case HL_HOPE_MAC_MD5: {
        /* MD5(key || data) */
        hl_md5_ctx_t *ctx = hl_md5_init();
        hl_md5_update(ctx, key, key_len);
        hl_md5_update(ctx, data, data_len);
        hl_md5_final(ctx, out);
        *out_len = MD5_DIGEST_SIZE;
        break;
    }

    case HL_HOPE_MAC_INVERSE: {
        /* Bitwise NOT of key bytes; data is ignored */
        size_t i;
        for (i = 0; i < key_len; i++)
            out[i] = (uint8_t)(~key[i]);
        *out_len = key_len;
        break;
    }

    default:
        *out_len = 0;
        break;
    }
}

/* ====================================================================
 * HKDF-SHA256 (RFC 5869)
 *
 * Extract: PRK = HMAC-SHA256(salt, IKM)
 * Expand:  OKM = HMAC-SHA256(PRK, info || 0x01) (for out_len <= 32)
 * ==================================================================== */

void hl_hkdf_sha256(const uint8_t *ikm, size_t ikm_len,
                    const uint8_t *salt, size_t salt_len,
                    const uint8_t *info, size_t info_len,
                    uint8_t *out, size_t out_len)
{
    /* Default salt: 32 zero bytes if not provided */
    uint8_t default_salt[SHA256_DIGEST_SIZE];
    if (!salt || salt_len == 0) {
        memset(default_salt, 0, SHA256_DIGEST_SIZE);
        salt = default_salt;
        salt_len = SHA256_DIGEST_SIZE;
    }

    /* Extract: PRK = HMAC-SHA256(salt, IKM) */
    uint8_t prk[SHA256_DIGEST_SIZE];
    hmac_sha256(salt, salt_len, ikm, ikm_len, prk);

    /* Expand: T(1) = HMAC-SHA256(PRK, info || 0x01) */
    /* For out_len <= 32, only one iteration is needed */
    uint8_t expand_input[256 + 1]; /* info + counter byte */
    size_t expand_len = 0;

    if (info && info_len > 0) {
        if (info_len > 256) info_len = 256; /* safety clamp */
        memcpy(expand_input, info, info_len);
        expand_len = info_len;
    }
    expand_input[expand_len] = 0x01; /* counter = 1 */
    expand_len += 1;

    uint8_t t1[SHA256_DIGEST_SIZE];
    hmac_sha256(prk, SHA256_DIGEST_SIZE, expand_input, expand_len, t1);

    /* Copy requested output length */
    size_t copy_len = out_len < SHA256_DIGEST_SIZE ? out_len : SHA256_DIGEST_SIZE;
    memcpy(out, t1, copy_len);
}

/* ====================================================================
 * Algorithm list encoding / decoding
 *
 * Wire format: <u16:count> [<u8:len> <str:name>]+
 * ==================================================================== */

/* Parse a name string and return the matching algorithm, or -1 */
static int parse_algorithm_name(const char *name, size_t name_len)
{
    int i;
    for (i = 0; i < HL_HOPE_MAC_COUNT; i++) {
        if (strlen(mac_alg_names[i]) == name_len &&
            strncasecmp(mac_alg_names[i], name, name_len) == 0) {
            return i;
        }
    }
    return -1;
}

/* ====================================================================
 * Security policy
 * ==================================================================== */

int hl_hope_algorithm_allowed(hl_hope_mac_alg_t alg, int legacy_mode)
{
    if (legacy_mode) return 1; /* allow everything per original spec */
    /* Strict mode: only HMAC variants (safe as MACs even with weakened hashes) */
    return (alg == HL_HOPE_MAC_HMAC_SHA256 ||
            alg == HL_HOPE_MAC_HMAC_SHA1 ||
            alg == HL_HOPE_MAC_HMAC_MD5);
}

hl_hope_cipher_policy_t hl_hope_parse_cipher_policy(const char *str)
{
    if (!str) return HL_HOPE_CIPHER_PREFER_AEAD;
    if (strcasecmp(str, "require-aead") == 0) return HL_HOPE_CIPHER_REQUIRE_AEAD;
    if (strcasecmp(str, "rc4-only") == 0) return HL_HOPE_CIPHER_RC4_ONLY;
    return HL_HOPE_CIPHER_PREFER_AEAD;
}

/* Check if a cipher list field contains a recognized RC4 variant.
 * Parses the <u16:count> [<u8:len> <str:name>]+ format.
 * Returns 1 if RC4/RC4-128/ARCFOUR found, 0 otherwise. */
static int hope_field_contains_rc4(const hl_field_t *f)
{
    if (!f || f->data_len < 2) return 0;

    uint16_t count = hl_read_u16(f->data);
    size_t pos = 2;
    uint16_t c;

    for (c = 0; c < count && pos < f->data_len; c++) {
        uint8_t name_len = f->data[pos++];
        if (pos + name_len > f->data_len) break;

        /* Check for known RC4 name variants */
        if ((name_len == 3 && strncasecmp((const char *)(f->data + pos), "RC4", 3) == 0) ||
            (name_len == 7 && strncasecmp((const char *)(f->data + pos), "RC4-128", 7) == 0) ||
            (name_len == 7 && strncasecmp((const char *)(f->data + pos), "ARCFOUR", 7) == 0)) {
            return 1;
        }
        pos += name_len;
    }
    return 0;
}

/* Check if a cipher list field contains a recognized ChaCha20-Poly1305 variant.
 * Returns 1 if CHACHA20-POLY1305/CHACHA20POLY1305/CHACHA20 found. */
static int hope_field_contains_chacha20(const hl_field_t *f)
{
    if (!f || f->data_len < 2) return 0;

    uint16_t count = hl_read_u16(f->data);
    size_t pos = 2;
    uint16_t c;

    for (c = 0; c < count && pos < f->data_len; c++) {
        uint8_t name_len = f->data[pos++];
        if (pos + name_len > f->data_len) break;

        if ((name_len == 17 && strncasecmp((const char *)(f->data + pos), "CHACHA20-POLY1305", 17) == 0) ||
            (name_len == 16 && strncasecmp((const char *)(f->data + pos), "CHACHA20POLY1305", 16) == 0) ||
            (name_len == 8  && strncasecmp((const char *)(f->data + pos), "CHACHA20", 8) == 0)) {
            return 1;
        }
        pos += name_len;
    }
    return 0;
}

hl_hope_mac_alg_t hl_hope_select_best_algorithm(const uint8_t *data,
                                                  size_t data_len,
                                                  int legacy_mode)
{
    if (data_len < 2) {
        return legacy_mode ? HL_HOPE_MAC_INVERSE : (hl_hope_mac_alg_t)-1;
    }

    uint16_t count = hl_read_u16(data);
    size_t pos = 2;

    /* Track which algorithms the client supports */
    int supported[HL_HOPE_MAC_COUNT];
    int i;
    for (i = 0; i < HL_HOPE_MAC_COUNT; i++) supported[i] = 0;

    uint16_t c;
    for (c = 0; c < count && pos < data_len; c++) {
        uint8_t name_len = data[pos++];
        if (pos + name_len > data_len) break;
        int idx = parse_algorithm_name((const char *)(data + pos), name_len);
        if (idx >= 0) supported[idx] = 1;
        pos += name_len;
    }

    /* Select strongest that passes security policy */
    for (i = 0; i < HL_HOPE_MAC_COUNT; i++) {
        if (supported[i] && hl_hope_algorithm_allowed((hl_hope_mac_alg_t)i, legacy_mode))
            return (hl_hope_mac_alg_t)i;
    }

    /* No acceptable algorithm found */
    return legacy_mode ? HL_HOPE_MAC_INVERSE : (hl_hope_mac_alg_t)-1;
}

int hl_hope_encode_algorithm_selection(hl_hope_mac_alg_t alg,
                                       uint8_t *buf, size_t buf_len)
{
    const char *name = mac_alg_names[alg];
    size_t name_len = strlen(name);
    size_t needed = 2 + 1 + name_len;

    if (buf_len < needed) return -1;

    hl_write_u16(buf, 1);                  /* count = 1 */
    buf[2] = (uint8_t)name_len;            /* name length */
    memcpy(buf + 3, name, name_len);       /* name string */

    return (int)needed;
}

int hl_hope_encode_cipher_selection(const char *name,
                                    uint8_t *buf, size_t buf_len)
{
    size_t name_len = strlen(name);
    size_t needed = 2 + 1 + name_len;

    if (buf_len < needed) return -1;

    hl_write_u16(buf, 1);
    buf[2] = (uint8_t)name_len;
    memcpy(buf + 3, name, name_len);

    return (int)needed;
}

/* ====================================================================
 * HOPE probe detection
 * ==================================================================== */

int hl_hope_detect_probe(const hl_transaction_t *login_tran)
{
    const hl_field_t *f = hl_transaction_get_field(login_tran, FIELD_USER_LOGIN);
    if (!f) return 0;
    return (f->data_len == 1 && f->data[0] == 0x00) ? 1 : 0;
}

/* ====================================================================
 * HOPE negotiation reply builder
 * ==================================================================== */

int hl_hope_build_negotiation_reply(hl_hope_state_t *state,
                                    const hl_transaction_t *probe,
                                    hl_transaction_t *reply,
                                    const char *server_ip,
                                    uint16_t server_port,
                                    int legacy_mode,
                                    hl_hope_cipher_policy_t cipher_policy,
                                    hl_hope_cipher_mode_t *out_cipher_mode)
{
    /* Parse client's MAC algorithm proposal */
    const hl_field_t *f_mac = hl_transaction_get_field(probe, FIELD_HOPE_MAC_ALGORITHM);
    if (f_mac) {
        state->mac_alg = hl_hope_select_best_algorithm(f_mac->data, f_mac->data_len,
                                                        legacy_mode);
    } else {
        /* No MAC field at all */
        state->mac_alg = legacy_mode ? HL_HOPE_MAC_INVERSE : (hl_hope_mac_alg_t)-1;
    }

    /* In strict mode, reject if no acceptable algorithm was found */
    if ((int)state->mac_alg < 0) {
        *out_cipher_mode = HL_HOPE_CIPHER_MODE_NONE;
        return -1;
    }

    /* Generate 64-byte session key: IP(4) + Port(2) + Random(58) */
    memset(state->session_key, 0, 64);

    /* Server IP as 4-byte big-endian IPv4 address */
    struct in_addr addr;
    if (server_ip && inet_pton(AF_INET, server_ip, &addr) == 1) {
        memcpy(state->session_key, &addr.s_addr, 4);
    }

    /* Server port as 2-byte big-endian */
    hl_write_u16(state->session_key + 4, server_port);

    /* 58 random bytes */
    if (random_bytes(state->session_key + 6, 58) < 0)
        return -1;

    /* Check client cipher support */
    const hl_field_t *f_ccip = hl_transaction_get_field(probe, FIELD_HOPE_CLIENT_CIPHER);
    const hl_field_t *f_scip = hl_transaction_get_field(probe, FIELD_HOPE_SERVER_CIPHER);
    int client_wants_rc4 = hope_field_contains_rc4(f_ccip) || hope_field_contains_rc4(f_scip);
    int client_wants_aead = hope_field_contains_chacha20(f_ccip) || hope_field_contains_chacha20(f_scip);
    int mac_supports_aead = (state->mac_alg != HL_HOPE_MAC_INVERSE);

    fprintf(stderr, "[HOPE] Client offers: RC4=%d, AEAD=%d | Selected MAC=%s (%d) | MAC supports AEAD=%d | Policy=%d\n",
            client_wants_rc4, client_wants_aead,
            mac_alg_names[state->mac_alg], (int)state->mac_alg,
            mac_supports_aead, (int)cipher_policy);

    /* Select cipher mode based on policy */
    hl_hope_cipher_mode_t cipher_mode = HL_HOPE_CIPHER_MODE_NONE;

    switch (cipher_policy) {
    case HL_HOPE_CIPHER_PREFER_AEAD:
        if (client_wants_aead && mac_supports_aead)
            cipher_mode = HL_HOPE_CIPHER_MODE_AEAD;
        else if (client_wants_rc4 && mac_supports_aead)
            cipher_mode = HL_HOPE_CIPHER_MODE_RC4;
        /* else: auth-only (INVERSE or no cipher support) */
        break;

    case HL_HOPE_CIPHER_REQUIRE_AEAD:
        if (client_wants_aead && mac_supports_aead) {
            cipher_mode = HL_HOPE_CIPHER_MODE_AEAD;
        } else {
            /* Client doesn't support AEAD or MAC is INVERSE — reject */
            *out_cipher_mode = HL_HOPE_CIPHER_MODE_NONE;
            return -1;
        }
        break;

    case HL_HOPE_CIPHER_RC4_ONLY:
        if (client_wants_rc4 && mac_supports_aead)
            cipher_mode = HL_HOPE_CIPHER_MODE_RC4;
        break;
    }

    *out_cipher_mode = cipher_mode;

    /* Build reply transaction fields */
    /* Max fields: session_key, mac_alg, user_login, app_id, app_string,
     * server_cipher, client_cipher, server_cipher_mode, client_cipher_mode,
     * server_checksum, client_checksum, server_compress, client_compress = 13 */
    hl_field_t fields[13];
    int fc = 0;

    /* Session key (64 bytes) */
    hl_field_new(&fields[fc++], FIELD_HOPE_SESSION_KEY, state->session_key, 64);

    /* Selected MAC algorithm */
    uint8_t alg_buf[32];
    int alg_len = hl_hope_encode_algorithm_selection(state->mac_alg, alg_buf, sizeof(alg_buf));
    if (alg_len > 0) {
        hl_field_new(&fields[fc++], FIELD_HOPE_MAC_ALGORITHM, alg_buf, (uint16_t)alg_len);
    }

    /* UserLogin field — non-empty signals client should MAC the login.
     * Send the algorithm name as the content (per spec convention). */
    const char *alg_name = mac_alg_names[state->mac_alg];
    hl_field_new(&fields[fc++], FIELD_USER_LOGIN,
                 (const uint8_t *)alg_name, (uint16_t)strlen(alg_name));

    /* App identification */
    hl_field_new(&fields[fc++], FIELD_HOPE_APP_ID,
                 (const uint8_t *)HOPE_APP_ID, (uint16_t)strlen(HOPE_APP_ID));
    hl_field_new(&fields[fc++], FIELD_HOPE_APP_STRING,
                 (const uint8_t *)HOPE_APP_STRING, (uint16_t)strlen(HOPE_APP_STRING));

    /* Cipher selections */
    if (cipher_mode == HL_HOPE_CIPHER_MODE_AEAD) {
        /* CHACHA20-POLY1305 with AEAD mode fields */
        uint8_t cipher_buf[32];
        int cipher_len = hl_hope_encode_cipher_selection("CHACHA20-POLY1305", cipher_buf, sizeof(cipher_buf));
        if (cipher_len > 0) {
            hl_field_new(&fields[fc++], FIELD_HOPE_SERVER_CIPHER,
                         cipher_buf, (uint16_t)cipher_len);
            hl_field_new(&fields[fc++], FIELD_HOPE_CLIENT_CIPHER,
                         cipher_buf, (uint16_t)cipher_len);
        }
        /* CipherMode fields: "AEAD" as raw ASCII */
        hl_field_new(&fields[fc++], FIELD_HOPE_SERVER_CIPHER_MODE,
                     (const uint8_t *)"AEAD", 4);
        hl_field_new(&fields[fc++], FIELD_HOPE_CLIENT_CIPHER_MODE,
                     (const uint8_t *)"AEAD", 4);
        /* Checksum fields: "AEAD" */
        hl_field_new(&fields[fc++], FIELD_HOPE_SERVER_CHECKSUM,
                     (const uint8_t *)"AEAD", 4);
        hl_field_new(&fields[fc++], FIELD_HOPE_CLIENT_CHECKSUM,
                     (const uint8_t *)"AEAD", 4);
    } else if (cipher_mode == HL_HOPE_CIPHER_MODE_RC4) {
        uint8_t cipher_buf[16];
        int cipher_len = hl_hope_encode_cipher_selection("RC4", cipher_buf, sizeof(cipher_buf));
        if (cipher_len > 0) {
            hl_field_new(&fields[fc++], FIELD_HOPE_SERVER_CIPHER,
                         cipher_buf, (uint16_t)cipher_len);
            hl_field_new(&fields[fc++], FIELD_HOPE_CLIENT_CIPHER,
                         cipher_buf, (uint16_t)cipher_len);
        }
    }

    /* Compression: NONE */
    {
        uint8_t none_buf[16];
        int none_len = hl_hope_encode_cipher_selection("NONE", none_buf, sizeof(none_buf));
        if (none_len > 0) {
            hl_field_new(&fields[fc++], FIELD_HOPE_SERVER_COMPRESS,
                         none_buf, (uint16_t)none_len);
            hl_field_new(&fields[fc++], FIELD_HOPE_CLIENT_COMPRESS,
                         none_buf, (uint16_t)none_len);
        }
    }

    /* Build the reply as a reply to the probe's login transaction */
    memset(reply, 0, sizeof(*reply));
    reply->is_reply = 1;
    memcpy(reply->id, probe->id, 4);
    memcpy(reply->type, TRAN_LOGIN, 2);

    /* Copy fields into reply */
    reply->fields = (hl_field_t *)calloc((size_t)fc, sizeof(hl_field_t));
    if (!reply->fields) {
        int i;
        for (i = 0; i < fc; i++) hl_field_free(&fields[i]);
        return -1;
    }
    memcpy(reply->fields, fields, (size_t)fc * sizeof(hl_field_t));
    reply->field_count = (uint16_t)fc;

    /* Compute payload size */
    uint32_t payload = hl_transaction_payload_size(reply);
    hl_write_u32(reply->total_size, payload);
    hl_write_u32(reply->data_size, payload);

    /* Initialize HOPE state for later */
    state->active = 0;
    state->aead_active = 0;
    state->decrypt_phase = HOPE_PHASE_HEADER;
    state->decrypt_offset = 0;
    state->current_body_len = 0;
    state->body_rest_remaining = 0;
    state->rotation_count = 0;
    memset(&state->aead, 0, sizeof(state->aead));

    return 0;
}

/* ====================================================================
 * HOPE login verification
 * ==================================================================== */

int hl_hope_verify_login(hl_hope_state_t *state,
                         const hl_field_t *f_login,
                         const hl_field_t *f_password,
                         hl_account_mgr_t *acct_mgr,
                         hl_account_t **out_acct)
{
    if (!f_login || !acct_mgr) return 0;

    int count = 0;
    hl_account_t **accounts = acct_mgr->vt->list(acct_mgr, &count);
    if (!accounts) return 0;

    uint8_t mac_buf[64];
    size_t mac_len;
    int i;

    for (i = 0; i < count; i++) {
        const char *login = accounts[i]->login;
        const char *pw = accounts[i]->password;

        /* Skip accounts with hashed passwords — HOPE needs plaintext */
        if (strncmp(pw, "sha1:", 5) == 0) continue;

        /* Compute MAC of this account's login name
         * Spec: MAC(key=login, msg=session_key) */
        hl_hope_mac(state->mac_alg,
                    state->session_key, 64,
                    (const uint8_t *)login, strlen(login),
                    mac_buf, &mac_len);

        /* Compare with client's MAC'd login */
        if (mac_len != f_login->data_len ||
            memcmp(mac_buf, f_login->data, mac_len) != 0) {
            continue;
        }

        /* Login matched — now verify password */
        if (!f_password || f_password->data_len == 0) {
            /* Client sent no password — account must have empty password */
            if (pw[0] != '\0') {
                free(accounts);
                return 0;
            }
            *out_acct = accounts[i];
            free(accounts);
            return 1;
        }

        /* Compute MAC of this account's password
         * Spec: MAC(key=password, msg=session_key) */
        hl_hope_mac(state->mac_alg,
                    state->session_key, 64,
                    (const uint8_t *)pw, strlen(pw),
                    mac_buf, &mac_len);

        if (mac_len == f_password->data_len &&
            memcmp(mac_buf, f_password->data, mac_len) == 0) {
            *out_acct = accounts[i];
            free(accounts);
            return 1;
        }

        /* Login matched but password didn't */
        free(accounts);
        return 0;
    }

    free(accounts);
    return 0;
}

/* ====================================================================
 * Transport key derivation
 * ==================================================================== */

int hl_hope_derive_keys(hl_hope_state_t *state, const char *password)
{
    /* INVERSE MAC cannot derive transport keys */
    if (state->mac_alg == HL_HOPE_MAC_INVERSE) {
        memset(&state->encrypt, 0, sizeof(state->encrypt));
        memset(&state->decrypt, 0, sizeof(state->decrypt));
        return 0;
    }

    const uint8_t *pw = (const uint8_t *)password;
    size_t pw_len = strlen(password);

    /* password_mac = MAC(key=password, msg=session_key) */
    uint8_t password_mac[64];
    size_t pm_len;
    hl_hope_mac(state->mac_alg,
                state->session_key, 64,
                pw, pw_len,
                password_mac, &pm_len);

    /* encode_key = MAC(key=password, msg=password_mac) — server outbound */
    uint8_t encode_key[64];
    size_t ek_len;
    hl_hope_mac(state->mac_alg,
                password_mac, pm_len,
                pw, pw_len,
                encode_key, &ek_len);

    /* decode_key = MAC(key=password, msg=encode_key) — server inbound */
    uint8_t decode_key[64];
    size_t dk_len;
    hl_hope_mac(state->mac_alg,
                encode_key, ek_len,
                pw, pw_len,
                decode_key, &dk_len);

    /* Initialize encrypt cipher (server -> client) */
    memcpy(state->encrypt.current_key, encode_key, ek_len);
    state->encrypt.key_len = ek_len;
    hl_rc4_init(&state->encrypt.rc4, encode_key, ek_len);

    /* Initialize decrypt cipher (client -> server) */
    memcpy(state->decrypt.current_key, decode_key, dk_len);
    state->decrypt.key_len = dk_len;
    hl_rc4_init(&state->decrypt.rc4, decode_key, dk_len);

    return 0;
}

/* ====================================================================
 * AEAD transport key derivation
 * ==================================================================== */

int hl_hope_aead_derive_keys(hl_hope_state_t *state, const char *password)
{
    /* INVERSE MAC cannot derive AEAD keys */
    if (state->mac_alg == HL_HOPE_MAC_INVERSE) {
        memset(&state->aead, 0, sizeof(state->aead));
        return -1;
    }

    const uint8_t *pw = (const uint8_t *)password;
    size_t pw_len = strlen(password);

    /* password_mac = MAC(key=password, msg=session_key) */
    uint8_t password_mac[64];
    size_t pm_len;
    hl_hope_mac(state->mac_alg,
                state->session_key, 64,
                pw, pw_len,
                password_mac, &pm_len);

    /* encode_key = MAC(key=password, msg=password_mac) */
    uint8_t encode_key[64];
    size_t ek_len;
    hl_hope_mac(state->mac_alg,
                password_mac, pm_len,
                pw, pw_len,
                encode_key, &ek_len);

    /* decode_key = MAC(key=password, msg=encode_key) */
    uint8_t decode_key[64];
    size_t dk_len;
    hl_hope_mac(state->mac_alg,
                encode_key, ek_len,
                pw, pw_len,
                decode_key, &dk_len);

    /* HKDF-expand to 256-bit keys */
    hl_hkdf_sha256(encode_key, ek_len,
                   state->session_key, 64,
                   (const uint8_t *)"hope-chacha-encode", 18,
                   state->aead.encode_key, 32);

    hl_hkdf_sha256(decode_key, dk_len,
                   state->session_key, 64,
                   (const uint8_t *)"hope-chacha-decode", 18,
                   state->aead.decode_key, 32);

    /* Derive file transfer base key:
     * HKDF(ikm = encode_key_256 || decode_key_256, salt = session_key, info = "hope-file-transfer") */
    uint8_t ft_ikm[64];
    memcpy(ft_ikm, state->aead.encode_key, 32);
    memcpy(ft_ikm + 32, state->aead.decode_key, 32);
    hl_hkdf_sha256(ft_ikm, 64,
                   state->session_key, 64,
                   (const uint8_t *)"hope-file-transfer", 18,
                   state->aead.ft_base_key, 32);
    state->aead.ft_base_key_set = 1;

    /* Zero counters */
    state->aead.send_counter = 0;
    state->aead.recv_counter = 0;

    /* Zero intermediate key material */
    memset(password_mac, 0, sizeof(password_mac));
    memset(encode_key, 0, sizeof(encode_key));
    memset(decode_key, 0, sizeof(decode_key));
    memset(ft_ikm, 0, sizeof(ft_ikm));

    return 0;
}

int hl_hope_aead_derive_transfer_key(const hl_hope_state_t *state,
                                     const uint8_t ref_num[4],
                                     uint8_t out_key[32])
{
    if (!state->aead.ft_base_key_set)
        return -1;

    hl_hkdf_sha256(state->aead.ft_base_key, 32,
                   ref_num, 4,
                   (const uint8_t *)"hope-ft-ref", 11,
                   out_key, 32);
    return 0;
}

/* ====================================================================
 * Key rotation
 * ==================================================================== */

static void rotate_cipher_key(hl_hope_state_t *state, hl_hope_cipher_t *cipher)
{
    uint8_t new_key[64];
    size_t new_len;

    /* new_key = MAC(key=current_key, msg=session_key) */
    hl_hope_mac(state->mac_alg,
                state->session_key, 64,
                cipher->current_key, cipher->key_len,
                new_key, &new_len);

    memcpy(cipher->current_key, new_key, new_len);
    cipher->key_len = new_len;
    hl_rc4_init(&cipher->rc4, new_key, new_len);
}

/* ====================================================================
 * Outbound encryption
 * ==================================================================== */

void hl_hope_encrypt_transaction(hl_hope_state_t *state,
                                 uint8_t *buf, size_t len)
{
    if (len < 20) return;

    hl_hope_cipher_t *c = &state->encrypt;

    /* Encrypt 20-byte header (rotation_count = 0, no rotation on outbound) */
    hl_rc4_process(&c->rc4, buf, 20);

    if (len > 20) {
        /* Encrypt first 2 bytes of body */
        size_t body_len = len - 20;
        size_t first = (body_len < 2) ? body_len : 2;
        hl_rc4_process(&c->rc4, buf + 20, first);
        /* No rotation (count=0), encrypt rest directly */
        if (body_len > 2) {
            hl_rc4_process(&c->rc4, buf + 22, body_len - 2);
        }
    }
}

/* ====================================================================
 * AEAD transport functions (ChaCha20-Poly1305)
 * ==================================================================== */

/* Direction bytes for nonce construction */
#define AEAD_DIR_SERVER_TO_CLIENT 0x00
#define AEAD_DIR_CLIENT_TO_SERVER 0x01

static void aead_build_nonce(uint8_t nonce[12], uint8_t direction, uint64_t counter)
{
    memset(nonce, 0, 12);
    nonce[0] = direction;
    /* bytes 1-3: zero padding */
    /* bytes 4-11: big-endian u64 counter */
    nonce[4]  = (uint8_t)(counter >> 56);
    nonce[5]  = (uint8_t)(counter >> 48);
    nonce[6]  = (uint8_t)(counter >> 40);
    nonce[7]  = (uint8_t)(counter >> 32);
    nonce[8]  = (uint8_t)(counter >> 24);
    nonce[9]  = (uint8_t)(counter >> 16);
    nonce[10] = (uint8_t)(counter >> 8);
    nonce[11] = (uint8_t)(counter);
}

int hl_hope_aead_encrypt_transaction(hl_hope_state_t *state,
                                     const uint8_t *tx_buf, size_t tx_len,
                                     uint8_t *out_buf, size_t out_buf_size)
{
    size_t needed = 4 + tx_len + HL_POLY1305_TAG_SIZE;
    if (out_buf_size < needed) return -1;

    uint8_t nonce[12];
    aead_build_nonce(nonce, AEAD_DIR_SERVER_TO_CLIENT, state->aead.send_counter);

    /* Encrypt: plaintext -> ciphertext + tag */
    uint8_t *ciphertext = out_buf + 4;
    uint8_t *tag = ciphertext + tx_len;

    if (hl_chacha20_poly1305_encrypt(state->aead.encode_key, nonce,
                                      tx_buf, tx_len,
                                      ciphertext, tag) != 0) {
        return -1;
    }

    /* Write 4-byte BE length prefix (ciphertext + tag) */
    uint32_t frame_payload = (uint32_t)(tx_len + HL_POLY1305_TAG_SIZE);
    out_buf[0] = (uint8_t)(frame_payload >> 24);
    out_buf[1] = (uint8_t)(frame_payload >> 16);
    out_buf[2] = (uint8_t)(frame_payload >> 8);
    out_buf[3] = (uint8_t)(frame_payload);

    state->aead.send_counter++;
    return (int)needed;
}

int hl_hope_aead_scan_frame(const uint8_t *buf, size_t buf_len,
                            size_t max_frame_size)
{
    if (buf_len < 4) return 0; /* need at least the length prefix */

    uint32_t payload_len = ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
                           ((uint32_t)buf[2] << 8)  | (uint32_t)buf[3];

    if (payload_len < HL_POLY1305_TAG_SIZE) return -1; /* impossibly small */

    size_t total = 4 + (size_t)payload_len;
    if (total > max_frame_size) return -1; /* frame too large */
    if (buf_len < total) return 0;         /* need more data */

    return (int)total;
}

int hl_hope_aead_decrypt_frame(hl_hope_state_t *state,
                               const uint8_t *buf, size_t frame_len,
                               uint8_t *out_buf, size_t *out_len)
{
    if (frame_len < 4 + HL_POLY1305_TAG_SIZE) return -1;

    uint32_t payload_len = ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
                           ((uint32_t)buf[2] << 8)  | (uint32_t)buf[3];

    size_t ct_len = (size_t)payload_len - HL_POLY1305_TAG_SIZE;
    const uint8_t *ciphertext = buf + 4;
    const uint8_t *tag = ciphertext + ct_len;

    uint8_t nonce[12];
    aead_build_nonce(nonce, AEAD_DIR_CLIENT_TO_SERVER, state->aead.recv_counter);

    if (hl_chacha20_poly1305_decrypt(state->aead.decode_key, nonce,
                                      ciphertext, ct_len,
                                      tag, out_buf) != 0) {
        return -1; /* tag verification failed */
    }

    *out_len = ct_len;
    state->aead.recv_counter++;
    return 0;
}

/* ====================================================================
 * AEAD stream reader for file transfer uploads
 * ==================================================================== */

void hl_aead_reader_init(hl_aead_stream_reader_t *r,
                         hl_tls_conn_t *conn,
                         const uint8_t key[32])
{
    r->conn = conn;
    memcpy(r->key, key, 32);
    r->recv_counter = 0;
    r->buffer = NULL;
    r->buffer_len = 0;
    r->buffer_off = 0;
}

void hl_aead_reader_free(hl_aead_stream_reader_t *r)
{
    if (r->buffer) {
        free(r->buffer);
        r->buffer = NULL;
    }
    r->buffer_len = 0;
    r->buffer_off = 0;
    memset(r->key, 0, 32);
}

/* Read the next AEAD frame from the connection, decrypt it, and buffer the plaintext. */
static int aead_reader_fill(hl_aead_stream_reader_t *r)
{
    /* Read 4-byte frame length prefix */
    uint8_t len_buf[4];
    if (hl_conn_read_full(r->conn, len_buf, 4) < 0)
        return -1;

    uint32_t payload_len = ((uint32_t)len_buf[0] << 24) | ((uint32_t)len_buf[1] << 16) |
                           ((uint32_t)len_buf[2] << 8)  | (uint32_t)len_buf[3];

    if (payload_len < HL_POLY1305_TAG_SIZE || payload_len > 16 * 1024 * 1024)
        return -1;

    /* Read ciphertext + tag */
    uint8_t *frame_data = (uint8_t *)malloc(payload_len);
    if (!frame_data) return -1;

    if (hl_conn_read_full(r->conn, frame_data, payload_len) < 0) {
        free(frame_data);
        return -1;
    }

    size_t ct_len = payload_len - HL_POLY1305_TAG_SIZE;
    const uint8_t *ciphertext = frame_data;
    const uint8_t *tag = frame_data + ct_len;

    /* Build nonce: client->server direction (0x01) */
    uint8_t nonce[12];
    memset(nonce, 0, 12);
    nonce[0] = AEAD_DIR_CLIENT_TO_SERVER;
    nonce[4]  = (uint8_t)(r->recv_counter >> 56);
    nonce[5]  = (uint8_t)(r->recv_counter >> 48);
    nonce[6]  = (uint8_t)(r->recv_counter >> 40);
    nonce[7]  = (uint8_t)(r->recv_counter >> 32);
    nonce[8]  = (uint8_t)(r->recv_counter >> 24);
    nonce[9]  = (uint8_t)(r->recv_counter >> 16);
    nonce[10] = (uint8_t)(r->recv_counter >> 8);
    nonce[11] = (uint8_t)(r->recv_counter);

    /* Decrypt */
    uint8_t *plaintext = (uint8_t *)malloc(ct_len);
    if (!plaintext) { free(frame_data); return -1; }

    if (hl_chacha20_poly1305_decrypt(r->key, nonce, ciphertext, ct_len,
                                      tag, plaintext) != 0) {
        fprintf(stderr, "[HOPE-AEAD-FT-R] Tag verification failed, frame=%u bytes, counter=%llu\n",
                payload_len, (unsigned long long)r->recv_counter);
        free(frame_data);
        free(plaintext);
        return -1;
    }

    r->recv_counter++;
    free(frame_data);

    /* Replace buffer with new plaintext */
    if (r->buffer) free(r->buffer);
    r->buffer = plaintext;
    r->buffer_len = ct_len;
    r->buffer_off = 0;

    return 0;
}

int hl_aead_reader_read_full(hl_aead_stream_reader_t *r,
                             uint8_t *out, size_t len)
{
    size_t filled = 0;
    while (filled < len) {
        size_t avail = r->buffer_len - r->buffer_off;
        if (avail > 0) {
            size_t to_copy = len - filled;
            if (to_copy > avail) to_copy = avail;
            memcpy(out + filled, r->buffer + r->buffer_off, to_copy);
            r->buffer_off += to_copy;
            filled += to_copy;
        } else {
            if (aead_reader_fill(r) < 0)
                return -1;
        }
    }
    return 0;
}

ssize_t hl_aead_reader_read(hl_aead_stream_reader_t *r,
                            uint8_t *out, size_t len)
{
    size_t avail = r->buffer_len - r->buffer_off;
    if (avail == 0) {
        if (aead_reader_fill(r) < 0)
            return -1;
        avail = r->buffer_len - r->buffer_off;
    }

    size_t to_copy = len < avail ? len : avail;
    memcpy(out, r->buffer + r->buffer_off, to_copy);
    r->buffer_off += to_copy;
    return (ssize_t)to_copy;
}

/* ====================================================================
 * Inbound incremental decryption (state machine) — RC4
 * ==================================================================== */

size_t hl_hope_decrypt_incremental(hl_hope_state_t *state,
                                   uint8_t *buf, size_t buf_len)
{
    hl_hope_cipher_t *c = &state->decrypt;

    while (state->decrypt_offset < buf_len) {
        size_t avail = buf_len - state->decrypt_offset;

        switch (state->decrypt_phase) {
        case HOPE_PHASE_HEADER:
            if (avail < 20) return state->decrypt_offset;

            /* Decrypt 20-byte header */
            hl_rc4_process(&c->rc4, buf + state->decrypt_offset, 20);

            /* Extract rotation count from header[0] (flags byte).
             * Per Janus interop: rotation is carried in the first byte
             * of the encrypted header, not the type field. */
            state->rotation_count = buf[state->decrypt_offset + 0];
            buf[state->decrypt_offset + 0] = 0; /* clear for transaction scanner */

            /* Parse body size from decrypted header bytes 16-19 (data_size) */
            state->current_body_len = (size_t)hl_read_u32(
                buf + state->decrypt_offset + 16);

            state->decrypt_offset += 20;

            if (state->current_body_len == 0) {
                /* No body — apply rotation if needed and stay in HEADER phase */
                uint8_t r;
                for (r = 0; r < state->rotation_count; r++)
                    rotate_cipher_key(state, c);
                state->decrypt_phase = HOPE_PHASE_HEADER;
            } else {
                state->decrypt_phase = HOPE_PHASE_BODY_PREFIX;
            }
            break;

        case HOPE_PHASE_BODY_PREFIX: {
            /* Decrypt first 2 bytes of body (or fewer if body < 2) */
            size_t prefix_len = (state->current_body_len < 2)
                              ? state->current_body_len : 2;
            if (avail < prefix_len) return state->decrypt_offset;

            hl_rc4_process(&c->rc4, buf + state->decrypt_offset, prefix_len);
            state->decrypt_offset += prefix_len;

            /* Apply key rotation */
            uint8_t r;
            for (r = 0; r < state->rotation_count; r++)
                rotate_cipher_key(state, c);

            if (state->current_body_len <= 2) {
                /* Body fully decrypted */
                state->decrypt_phase = HOPE_PHASE_HEADER;
            } else {
                state->body_rest_remaining = state->current_body_len - 2;
                state->decrypt_phase = HOPE_PHASE_BODY_REST;
            }
            break;
        }

        case HOPE_PHASE_BODY_REST: {
            size_t rest_len = state->body_rest_remaining;
            if (avail < rest_len) {
                /* Partial body — decrypt what we have */
                hl_rc4_process(&c->rc4, buf + state->decrypt_offset, avail);
                state->decrypt_offset += avail;
                state->body_rest_remaining -= avail;
                return state->decrypt_offset;
            }

            /* Full remaining body available */
            hl_rc4_process(&c->rc4, buf + state->decrypt_offset, rest_len);
            state->decrypt_offset += rest_len;
            state->decrypt_phase = HOPE_PHASE_HEADER;
            break;
        }
        }
    }

    return state->decrypt_offset;
}

void hl_hope_adjust_offset(hl_hope_state_t *state, size_t consumed)
{
    if (consumed >= state->decrypt_offset) {
        state->decrypt_offset = 0;
    } else {
        state->decrypt_offset -= consumed;
    }
}

/* ====================================================================
 * Cleanup
 * ==================================================================== */

void hl_hope_state_free(hl_hope_state_t *state)
{
    if (!state) return;
    /* Zero out key material */
    memset(state->session_key, 0, 64);
    memset(&state->encrypt, 0, sizeof(state->encrypt));
    memset(&state->decrypt, 0, sizeof(state->decrypt));
    memset(&state->aead, 0, sizeof(state->aead));
}

/* ====================================================================
 * Encryption-gated content access
 * ==================================================================== */

int hl_client_is_encrypted(const hl_client_conn_t *cc)
{
    if (!cc->hope) return 0;

    /* Check for active transport encryption (either RC4 or AEAD) */
    int has_transport = (cc->hope->active || cc->hope->aead_active) &&
                        cc->hope->mac_alg != HL_HOPE_MAC_INVERSE;
    if (!has_transport) return 0;

    /* If E2E requires AEAD specifically, RC4-only clients don't qualify.
     * This ensures file transfers are also encrypted (RC4 doesn't encrypt them). */
    if (cc->server && cc->server->config.e2e_require_aead && !cc->hope->aead_active)
        return 0;

    /* If E2E requires TLS, the client must also be on a TLS connection
     * so that file transfers (separate TCP connection) are encrypted. */
    if (cc->server && cc->server->config.e2e_require_tls && !cc->is_tls)
        return 0;

    return 1;
}

int hl_hope_name_requires_encryption(const char *name, size_t name_len,
                                     const char *prefix)
{
    if (!prefix || prefix[0] == '\0') return 0;
    if (!name) return 0;

    size_t prefix_len = strlen(prefix);
    if (name_len < prefix_len) return 0;

    return (strncasecmp(name, prefix, prefix_len) == 0) ? 1 : 0;
}

int hl_hope_path_requires_encryption(const char *full_path,
                                     const char *file_root,
                                     const char *prefix)
{
    if (!prefix || prefix[0] == '\0') return 0;
    if (!full_path || !file_root) return 0;

    size_t root_len = strlen(file_root);
    size_t prefix_len = strlen(prefix);

    /* Skip past the file_root portion */
    const char *rel = full_path;
    if (strncmp(full_path, file_root, root_len) == 0) {
        rel = full_path + root_len;
        if (*rel == '/') rel++;
    }

    /* Check each path component */
    while (*rel) {
        /* Skip leading slashes */
        while (*rel == '/') rel++;
        if (*rel == '\0') break;

        /* Find end of this component */
        const char *end = strchr(rel, '/');
        size_t comp_len = end ? (size_t)(end - rel) : strlen(rel);

        if (comp_len >= prefix_len &&
            strncasecmp(rel, prefix, prefix_len) == 0) {
            return 1;
        }

        rel += comp_len;
    }

    return 0;
}
