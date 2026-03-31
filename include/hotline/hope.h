/*
 * hope.h - HOPE Secure Login (Hotline Open Protocol Extensions)
 *
 * Community-developed challenge-response authentication and optional
 * transport encryption for the Hotline protocol. Implements MAC-based
 * auth with session keys and RC4/Blowfish stream encryption.
 *
 * Spec: fogWraith/Hotline HOPE-Secure-Login.md
 *
 * All crypto uses OpenSSL 0.9.7 APIs available on Tiger 10.4.
 * Remove this file and hope.c if HOPE support is dropped.
 */

#ifndef HOTLINE_HOPE_H
#define HOTLINE_HOPE_H

#include "hotline/types.h"
#include "hotline/transaction.h"
#include <openssl/rc4.h>
#include <openssl/blowfish.h>
#include <stddef.h>

/* --- MAC Algorithm IDs (ordered weakest to strongest) --- */

#define HL_HOPE_MAC_INVERSE     0   /* 255-XOR fallback (required by spec) */
#define HL_HOPE_MAC_MD5         1
#define HL_HOPE_MAC_HMAC_MD5    2
#define HL_HOPE_MAC_SHA1        3
#define HL_HOPE_MAC_HMAC_SHA1   4   /* strongest */
#define HL_HOPE_MAC_COUNT       5

/* --- Cipher IDs --- */

#define HL_HOPE_CIPHER_NONE     0
#define HL_HOPE_CIPHER_RC4      1
#define HL_HOPE_CIPHER_BLOWFISH 2

/* --- Session key --- */

#define HL_HOPE_SESSION_KEY_LEN 64

/* --- HOPE Detection --- */

/*
 * hl_hope_detect - Check if a login transaction is a HOPE Phase 1 probe.
 * Returns 1 if the FIELD_USER_LOGIN is a single 0x00 byte (HOPE signal).
 */
int hl_hope_detect(const hl_transaction_t *login);

/* --- Session Key --- */

/*
 * hl_hope_generate_session_key - Build a 64-byte session key.
 * Format: server_ip(4 BE) + port(2 BE) + random(58).
 * Returns 0 on success, -1 if RAND_bytes fails.
 */
int hl_hope_generate_session_key(uint32_t server_ip, uint16_t port,
                                  uint8_t out[HL_HOPE_SESSION_KEY_LEN]);

/* --- MAC Algorithm Negotiation --- */

/*
 * hl_hope_parse_mac_list - Parse the MAC algorithm list from
 * FIELD_HOPE_MAC_ALGORITHM. Wire format: <u16:count> [<u8:len> <str:name>]+
 * Fills out_algos with algorithm IDs, returns count parsed (max max_algos).
 */
int hl_hope_parse_mac_list(const uint8_t *data, uint16_t data_len,
                            int *out_algos, int max_algos);

/*
 * hl_hope_select_mac - Pick the strongest algorithm from the client's
 * list that we also support. Returns algorithm ID, or HL_HOPE_MAC_INVERSE
 * if no match (INVERSE is always supported per spec).
 */
int hl_hope_select_mac(const int *client_algos, int count);

/*
 * hl_hope_mac_name - Return the string name for a MAC algorithm ID
 * (e.g., "HMAC-SHA1"). Used when building the server's reply field.
 */
const char *hl_hope_mac_name(int algo);

/* --- MAC Computation --- */

/*
 * hl_hope_compute_mac - Compute a MAC using the specified algorithm.
 * out must be large enough (20 bytes for SHA1, 16 for MD5).
 * Returns bytes written to out, or -1 on error.
 */
int hl_hope_compute_mac(int algo,
                         const uint8_t *key, size_t key_len,
                         const uint8_t *msg, size_t msg_len,
                         uint8_t *out, size_t out_len);

/* --- Password Verification --- */

/*
 * hl_hope_verify_password - Verify a HOPE Phase 3 password MAC.
 * Computes MAC(password, session_key) and compares to client_mac.
 * password is plaintext (decrypted from storage).
 * Returns 1 if match, 0 if mismatch.
 */
int hl_hope_verify_password(int algo,
                             const char *password,
                             const uint8_t session_key[HL_HOPE_SESSION_KEY_LEN],
                             const uint8_t *client_mac, size_t mac_len);

/* --- Cipher Key Derivation --- */

/*
 * hl_hope_derive_keys - Derive encode/decode keys for transport encryption.
 * encode_key = MAC(password_bytes, password_mac)
 * decode_key = MAC(password_bytes, encode_key)
 * Returns key length written, or -1 on error.
 */
int hl_hope_derive_keys(int mac_algo,
                         const uint8_t *password, size_t pw_len,
                         const uint8_t *pw_mac, size_t mac_len,
                         uint8_t *encode_key, uint8_t *decode_key,
                         size_t key_buf_len);

/* --- Cipher Negotiation --- */

/*
 * hl_hope_parse_cipher - Parse a cipher name string to cipher ID.
 * Accepts "RC4", "BLOWFISH", "NONE".
 */
int hl_hope_parse_cipher(const char *name);

/* --- Transport Encryption I/O --- */

/* Forward declaration — full struct in client_conn.h */
struct hl_client_conn;

/*
 * hl_hope_init_rc4 - Initialize RC4 cipher state on a connection.
 * Call after key derivation. Sets cc->hope_encrypted = 1.
 */
void hl_hope_init_rc4(struct hl_client_conn *cc,
                       const uint8_t *encode_key, size_t encode_len,
                       const uint8_t *decode_key, size_t decode_len);

/*
 * hl_hope_read - Read from a client connection, decrypting if HOPE active.
 * If cc->hope_encrypted: reads raw bytes, then RC4 decrypts in-place.
 * If not: plain read() passthrough.
 * Returns bytes read, or -1 on error (same semantics as read()).
 */
ssize_t hl_hope_read(struct hl_client_conn *cc, uint8_t *buf, size_t len);

/*
 * hl_hope_write - Write to a client connection, encrypting if HOPE active.
 * If cc->hope_encrypted: RC4 encrypts then writes.
 * If not: plain write() passthrough.
 * Returns bytes written, or -1 on error (same semantics as write()).
 */
ssize_t hl_hope_write(struct hl_client_conn *cc, const uint8_t *buf, size_t len);

/* --- HOPE Password Storage --- */

/*
 * hl_hope_master_key_load - Load or generate the HOPE master key.
 * Looks for <config_dir>/hope.key (32 bytes). If not found, generates
 * one with RAND_bytes and writes it. Returns 0 on success.
 */
int hl_hope_master_key_load(const char *config_dir, uint8_t key[32]);

/*
 * hl_hope_password_encrypt - Encrypt a plaintext password for storage.
 * Output format: "hope:<hex>". Uses RC4 with master key.
 * Returns 0 on success.
 */
int hl_hope_password_encrypt(const uint8_t master_key[32],
                              const char *password,
                              char *out, size_t out_len);

/*
 * hl_hope_password_decrypt - Decrypt a stored HOPE password.
 * Input format: "hope:<hex>". Returns plaintext in out.
 * Returns 0 on success, -1 if format is invalid.
 */
int hl_hope_password_decrypt(const uint8_t master_key[32],
                              const char *stored,
                              char *out, size_t out_len);

/* --- Secure Zone --- */

/*
 * hl_hope_is_secure_zone - Check if a file path or news category
 * requires HOPE encryption. Returns 1 if the path contains "Encrypted".
 */
int hl_hope_is_secure_zone(const char *path);

#endif /* HOTLINE_HOPE_H */
