/*
 * hope.c - HOPE Secure Login (Hotline Open Protocol Extensions)
 *
 * Implements challenge-response authentication and optional RC4/Blowfish
 * transport encryption per the fogWraith HOPE-Secure-Login spec.
 *
 * All crypto uses OpenSSL 0.9.7 APIs available on Tiger 10.4.
 * Remove this file if HOPE support is dropped.
 */

#include "hotline/hope.h"
#include "hotline/client_conn.h"
#include "hotline/tls.h"
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <openssl/md5.h>
#include <openssl/rand.h>
#include <openssl/rc4.h>
#include <openssl/blowfish.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <strings.h>  /* strcasecmp, strcasestr */

/* ====================================================================
 * HOPE Detection
 * ==================================================================== */

int hl_hope_detect(const hl_transaction_t *login)
{
    const hl_field_t *f_login = hl_transaction_get_field(login, FIELD_USER_LOGIN);
    if (!f_login) return 0;
    /* HOPE Phase 1: login field is a single null byte */
    return (f_login->data_len == 1 && f_login->data[0] == 0x00);
}

/* ====================================================================
 * Session Key Generation
 * ==================================================================== */

int hl_hope_generate_session_key(uint32_t server_ip, uint16_t port,
                                  uint8_t out[HL_HOPE_SESSION_KEY_LEN])
{
    /* Format: IP(4 BE) + Port(2 BE) + Random(58) */
    out[0] = (uint8_t)(server_ip >> 24);
    out[1] = (uint8_t)(server_ip >> 16);
    out[2] = (uint8_t)(server_ip >> 8);
    out[3] = (uint8_t)(server_ip);
    out[4] = (uint8_t)(port >> 8);
    out[5] = (uint8_t)(port);

    if (RAND_bytes(out + 6, 58) != 1) {
        /* Fallback: time-based seed if /dev/urandom unavailable */
        unsigned int seed = (unsigned int)time(NULL) ^ (unsigned int)getpid();
        int i;
        for (i = 6; i < 64; i++) {
            seed = seed * 1103515245 + 12345;
            out[i] = (uint8_t)(seed >> 16);
        }
    }
    return 0;
}

/* ====================================================================
 * MAC Algorithm Negotiation
 * ==================================================================== */

/* Map algorithm name strings to IDs */
static struct { const char *name; int id; } mac_names[] = {
    {"HMAC-SHA1", HL_HOPE_MAC_HMAC_SHA1},
    {"SHA1",      HL_HOPE_MAC_SHA1},
    {"HMAC-MD5",  HL_HOPE_MAC_HMAC_MD5},
    {"MD5",       HL_HOPE_MAC_MD5},
    {"INVERSE",   HL_HOPE_MAC_INVERSE},
    {NULL, 0}
};

int hl_hope_parse_mac_list(const uint8_t *data, uint16_t data_len,
                            int *out_algos, int max_algos)
{
    /* Wire format: <u16:count> [<u8:len> <str:name>]+ */
    if (data_len < 2) return 0;

    uint16_t count = hl_read_u16(data);
    size_t offset = 2;
    int parsed = 0;

    uint16_t i;
    for (i = 0; i < count && offset < data_len && parsed < max_algos; i++) {
        uint8_t name_len = data[offset++];
        if (offset + name_len > data_len) break;

        /* Match name to algorithm ID */
        char name[64];
        size_t copy_len = name_len < sizeof(name) - 1 ? name_len : sizeof(name) - 1;
        memcpy(name, data + offset, copy_len);
        name[copy_len] = '\0';
        offset += name_len;

        int j;
        for (j = 0; mac_names[j].name; j++) {
            if (strcasecmp(name, mac_names[j].name) == 0) {
                out_algos[parsed++] = mac_names[j].id;
                break;
            }
        }
    }
    return parsed;
}

int hl_hope_select_mac(const int *client_algos, int count)
{
    /* Pick the strongest algorithm the client supports.
     * Our preference order (strongest first): HMAC-SHA1 > SHA1 > HMAC-MD5 > MD5 > INVERSE */
    int best = HL_HOPE_MAC_INVERSE;
    int i;
    for (i = 0; i < count; i++) {
        if (client_algos[i] > best)
            best = client_algos[i];
    }
    return best;
}

const char *hl_hope_mac_name(int algo)
{
    int i;
    for (i = 0; mac_names[i].name; i++) {
        if (mac_names[i].id == algo) return mac_names[i].name;
    }
    return "INVERSE";
}

/* ====================================================================
 * MAC Computation
 * ==================================================================== */

/* INVERSE: 255 - each byte (same as legacy obfuscation) */
static int compute_inverse(const uint8_t *data, size_t len,
                            uint8_t *out, size_t out_len)
{
    if (out_len < len) return -1;
    size_t i;
    for (i = 0; i < len; i++)
        out[i] = (uint8_t)(255 - data[i]);
    return (int)len;
}

int hl_hope_compute_mac(int algo,
                         const uint8_t *key, size_t key_len,
                         const uint8_t *msg, size_t msg_len,
                         uint8_t *out, size_t out_len)
{
    unsigned int result_len = 0;

    switch (algo) {
    case HL_HOPE_MAC_HMAC_SHA1:
        if (out_len < SHA_DIGEST_LENGTH) return -1;
        HMAC(EVP_sha1(), key, (int)key_len, msg, msg_len,
             out, &result_len);
        return (int)result_len;

    case HL_HOPE_MAC_SHA1: {
        /* SHA1(key + msg) */
        if (out_len < SHA_DIGEST_LENGTH) return -1;
        SHA_CTX ctx;
        SHA1_Init(&ctx);
        SHA1_Update(&ctx, key, key_len);
        SHA1_Update(&ctx, msg, msg_len);
        SHA1_Final(out, &ctx);
        return SHA_DIGEST_LENGTH;
    }

    case HL_HOPE_MAC_HMAC_MD5:
        if (out_len < MD5_DIGEST_LENGTH) return -1;
        HMAC(EVP_md5(), key, (int)key_len, msg, msg_len,
             out, &result_len);
        return (int)result_len;

    case HL_HOPE_MAC_MD5: {
        /* MD5(key + msg) */
        if (out_len < MD5_DIGEST_LENGTH) return -1;
        MD5_CTX ctx;
        MD5_Init(&ctx);
        MD5_Update(&ctx, key, key_len);
        MD5_Update(&ctx, msg, msg_len);
        MD5_Final(out, &ctx);
        return MD5_DIGEST_LENGTH;
    }

    case HL_HOPE_MAC_INVERSE:
        return compute_inverse(key, key_len, out, out_len);

    default:
        return -1;
    }
}

/* ====================================================================
 * Password Verification
 * ==================================================================== */

int hl_hope_verify_password(int algo,
                             const char *password,
                             const uint8_t session_key[HL_HOPE_SESSION_KEY_LEN],
                             const uint8_t *client_mac, size_t mac_len)
{
    uint8_t expected[SHA_DIGEST_LENGTH]; /* SHA1 is the largest (20 bytes) */
    int expected_len = hl_hope_compute_mac(algo,
        (const uint8_t *)password, strlen(password),
        session_key, HL_HOPE_SESSION_KEY_LEN,
        expected, sizeof(expected));

    if (expected_len < 0) return 0;
    if ((size_t)expected_len != mac_len) return 0;

    /* Constant-time comparison to prevent timing attacks */
    int diff = 0;
    int i;
    for (i = 0; i < expected_len; i++)
        diff |= expected[i] ^ client_mac[i];
    return (diff == 0);
}

/* ====================================================================
 * Cipher Key Derivation
 * ==================================================================== */

int hl_hope_derive_keys(int mac_algo,
                         const uint8_t *password, size_t pw_len,
                         const uint8_t *pw_mac, size_t mac_len,
                         uint8_t *encode_key, uint8_t *decode_key,
                         size_t key_buf_len)
{
    /* encode_key = MAC(password, password_mac) */
    int elen = hl_hope_compute_mac(mac_algo,
        password, pw_len, pw_mac, mac_len,
        encode_key, key_buf_len);
    if (elen < 0) return -1;

    /* decode_key = MAC(password, encode_key) */
    int dlen = hl_hope_compute_mac(mac_algo,
        password, pw_len, encode_key, (size_t)elen,
        decode_key, key_buf_len);
    if (dlen < 0) return -1;

    return elen; /* both keys same length */
}

/* ====================================================================
 * Cipher Negotiation
 * ==================================================================== */

int hl_hope_parse_cipher(const char *name)
{
    if (strcasecmp(name, "RC4") == 0) return HL_HOPE_CIPHER_RC4;
    if (strcasecmp(name, "BLOWFISH") == 0) return HL_HOPE_CIPHER_BLOWFISH;
    return HL_HOPE_CIPHER_NONE;
}

/* ====================================================================
 * Transport Encryption — RC4
 * ==================================================================== */

void hl_hope_init_rc4(struct hl_client_conn *cc,
                       const uint8_t *encode_key, size_t encode_len,
                       const uint8_t *decode_key, size_t decode_len)
{
    RC4_set_key(&cc->hope_rc4_encode, (int)encode_len, encode_key);
    RC4_set_key(&cc->hope_rc4_decode, (int)decode_len, decode_key);
    cc->hope_encrypted = 1;
}

ssize_t hl_hope_read(struct hl_client_conn *cc, uint8_t *buf, size_t len)
{
    /* Route through TLS conn wrapper if available, else raw read.
     * TLS decryption happens inside hl_conn_read; HOPE RC4 decryption
     * is layered on top (application-layer encryption over TLS). */
    ssize_t n;
    if (cc->conn) {
        n = hl_conn_read(cc->conn, buf, len);
    } else {
        n = read(cc->fd, buf, len);
    }
    if (n > 0 && cc->hope_encrypted) {
        /* Decrypt in-place — RC4 is a stream cipher, same operation
         * for encrypt and decrypt. The key state advances per-byte. */
        RC4(&cc->hope_rc4_decode, (unsigned long)n, buf, buf);
    }
    return n;
}

ssize_t hl_hope_write(struct hl_client_conn *cc, const uint8_t *buf, size_t len)
{
    if (!cc->hope_encrypted) {
        /* No HOPE encryption — write through conn wrapper (handles TLS) */
        if (cc->conn) {
            return (hl_conn_write_all(cc->conn, buf, len) == 0)
                   ? (ssize_t)len : -1;
        }
        /* Fallback to raw write_all for connections without a wrapper */
        size_t total = 0;
        while (total < len) {
            ssize_t w = write(cc->fd, buf + total, len - total);
            if (w < 0) { if (errno == EINTR) continue; return -1; }
            if (w == 0) return -1;
            total += (size_t)w;
        }
        return (ssize_t)len;
    }

    /* Encrypt into a temporary buffer, then write.
     * Allocate on stack for small transactions, heap for large. */
    uint8_t stack_buf[4096];
    uint8_t *enc_buf;
    int heap = 0;

    if (len <= sizeof(stack_buf)) {
        enc_buf = stack_buf;
    } else {
        enc_buf = (uint8_t *)malloc(len);
        if (!enc_buf) return -1;
        heap = 1;
    }

    RC4(&cc->hope_rc4_encode, (unsigned long)len, buf, enc_buf);
    int rc;
    if (cc->conn) {
        rc = hl_conn_write_all(cc->conn, enc_buf, len);
    } else {
        /* Fallback raw write_all */
        size_t total = 0;
        rc = 0;
        while (total < len) {
            ssize_t w = write(cc->fd, enc_buf + total, len - total);
            if (w < 0) { if (errno == EINTR) continue; rc = -1; break; }
            if (w == 0) { rc = -1; break; }
            total += (size_t)w;
        }
    }

    if (heap) free(enc_buf);
    return (rc == 0) ? (ssize_t)len : -1;
}

/* ====================================================================
 * HOPE Password Storage (master key + RC4 encryption at rest)
 * ==================================================================== */

int hl_hope_master_key_load(const char *config_dir, uint8_t key[32])
{
    char path[2048];
    snprintf(path, sizeof(path), "%s/hope.key", config_dir);

    /* Try to read existing key */
    int fd = open(path, O_RDONLY);
    if (fd >= 0) {
        ssize_t n = read(fd, key, 32);
        close(fd);
        if (n == 32) return 0;
        /* Short read — regenerate */
    }

    /* Generate new key */
    if (RAND_bytes(key, 32) != 1) {
        /* Fallback */
        unsigned int seed = (unsigned int)time(NULL) ^ (unsigned int)getpid();
        int i;
        for (i = 0; i < 32; i++) {
            seed = seed * 1103515245 + 12345;
            key[i] = (uint8_t)(seed >> 16);
        }
    }

    /* Write key file with restricted permissions */
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return -1;
    write(fd, key, 32);
    close(fd);

    return 0;
}

int hl_hope_password_encrypt(const uint8_t master_key[32],
                              const char *password,
                              char *out, size_t out_len)
{
    size_t pw_len = strlen(password);
    if (pw_len > 127) pw_len = 127;

    /* RC4 encrypt with master key */
    uint8_t encrypted[128];
    RC4_KEY rc4;
    RC4_set_key(&rc4, 32, master_key);
    RC4(&rc4, (unsigned long)pw_len, (const uint8_t *)password, encrypted);

    /* Encode as "hope:<hex>" */
    int needed = snprintf(out, out_len, "hope:");
    if ((size_t)needed >= out_len) return -1;

    size_t pos = 5; /* strlen("hope:") */
    size_t i;
    for (i = 0; i < pw_len && pos + 2 < out_len; i++) {
        sprintf(out + pos, "%02x", encrypted[i]);
        pos += 2;
    }
    out[pos] = '\0';

    /* Clear sensitive data */
    memset(encrypted, 0, sizeof(encrypted));
    memset(&rc4, 0, sizeof(rc4));

    return 0;
}

int hl_hope_password_decrypt(const uint8_t master_key[32],
                              const char *stored,
                              char *out, size_t out_len)
{
    /* Parse "hope:<hex>" */
    if (strncmp(stored, "hope:", 5) != 0) return -1;
    const char *hex = stored + 5;
    size_t hex_len = strlen(hex);
    size_t pw_len = hex_len / 2;
    if (pw_len == 0 || pw_len >= out_len) return -1;

    /* Decode hex */
    uint8_t encrypted[128];
    if (pw_len > sizeof(encrypted)) return -1;
    size_t i;
    for (i = 0; i < pw_len; i++) {
        unsigned int byte;
        if (sscanf(hex + i * 2, "%02x", &byte) != 1) return -1;
        encrypted[i] = (uint8_t)byte;
    }

    /* RC4 decrypt with master key (same operation as encrypt) */
    RC4_KEY rc4;
    RC4_set_key(&rc4, 32, master_key);
    RC4(&rc4, (unsigned long)pw_len, encrypted, (uint8_t *)out);
    out[pw_len] = '\0';

    /* Clear sensitive data */
    memset(encrypted, 0, sizeof(encrypted));
    memset(&rc4, 0, sizeof(rc4));

    return 0;
}

/* ====================================================================
 * Secure Zone Check
 * ==================================================================== */

int hl_hope_is_secure_zone(const char *path)
{
    if (!path) return 0;
    /* Check if path starts with or contains "Encrypted" (case-insensitive) */
    if (strncasecmp(path, "Encrypted", 9) == 0) return 1;
    if (strcasestr(path, "/Encrypted") != NULL) return 1;
    return 0;
}
