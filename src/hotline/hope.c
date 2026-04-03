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
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <arpa/inet.h>

/* --- Algorithm name table --- */

static const char *mac_alg_names[] = {
    "HMAC-SHA1",   /* HL_HOPE_MAC_HMAC_SHA1 */
    "SHA1",        /* HL_HOPE_MAC_SHA1 */
    "HMAC-MD5",    /* HL_HOPE_MAC_HMAC_MD5 */
    "MD5",         /* HL_HOPE_MAC_MD5 */
    "INVERSE"      /* HL_HOPE_MAC_INVERSE */
};

/* Server app identification */
#define HOPE_APP_ID     "LMSC"
#define HOPE_APP_STRING "Lemoniscate 0.1"

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

#define SHA1_BLOCK_SIZE   64
#define SHA1_DIGEST_SIZE  HL_SHA1_DIGEST_LENGTH   /* 20 */
#define MD5_BLOCK_SIZE    64
#define MD5_DIGEST_SIZE   HL_MD5_DIGEST_LENGTH    /* 16 */

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
    return (alg == HL_HOPE_MAC_HMAC_SHA1 || alg == HL_HOPE_MAC_HMAC_MD5);
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
                                    int legacy_mode)
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

    /* Check if client requested ciphers — validate actual cipher name */
    int client_wants_rc4 = 0;
    const hl_field_t *f_ccip = hl_transaction_get_field(probe, FIELD_HOPE_CLIENT_CIPHER);
    const hl_field_t *f_scip = hl_transaction_get_field(probe, FIELD_HOPE_SERVER_CIPHER);
    if (hope_field_contains_rc4(f_ccip) || hope_field_contains_rc4(f_scip)) {
        client_wants_rc4 = 1;
    }

    /* Determine if transport encryption is possible */
    int can_encrypt = client_wants_rc4 &&
                      (state->mac_alg != HL_HOPE_MAC_INVERSE);

    /* Build reply transaction fields */
    /* Max fields: session_key, mac_alg, user_login, app_id, app_string,
     * server_cipher, client_cipher, server_compress, client_compress = 9 */
    hl_field_t fields[9];
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
    if (can_encrypt) {
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
    state->decrypt_phase = HOPE_PHASE_HEADER;
    state->decrypt_offset = 0;
    state->current_body_len = 0;
    state->body_rest_remaining = 0;
    state->rotation_count = 0;

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
 * Inbound incremental decryption (state machine)
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
}

/* ====================================================================
 * Encryption-gated content access
 * ==================================================================== */

int hl_client_is_encrypted(const hl_client_conn_t *cc)
{
    if (!cc->hope || !cc->hope->active ||
        cc->hope->mac_alg == HL_HOPE_MAC_INVERSE)
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
