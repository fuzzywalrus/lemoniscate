/*
 * hope.h - HOPE (Hotline One-time Password Extension)
 *
 * Server-side implementation of the HOPE secure login and transport
 * encryption protocol. Provides MAC-based authentication and optional
 * RC4 stream cipher encryption for post-login traffic.
 *
 * Reference: https://github.com/fogWraith/Hotline/blob/main/Docs/Protocol/HOPE-Secure-Login.md
 */

#ifndef HOTLINE_HOPE_H
#define HOTLINE_HOPE_H

#include "hotline/types.h"
#include "hotline/field.h"
#include "hotline/transaction.h"
#include "hotline/client_conn.h"
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>  /* ssize_t */

/* --- HOPE field type constants --- */

static const hl_field_type_t FIELD_HOPE_APP_ID          = {0x0E, 0x01}; /* 3585 */
static const hl_field_type_t FIELD_HOPE_APP_STRING      = {0x0E, 0x02}; /* 3586 */
static const hl_field_type_t FIELD_HOPE_SESSION_KEY     = {0x0E, 0x03}; /* 3587 */
static const hl_field_type_t FIELD_HOPE_MAC_ALGORITHM   = {0x0E, 0x04}; /* 3588 */
static const hl_field_type_t FIELD_HOPE_SERVER_CIPHER   = {0x0E, 0xC1}; /* 3777 */
static const hl_field_type_t FIELD_HOPE_CLIENT_CIPHER   = {0x0E, 0xC2}; /* 3778 */
static const hl_field_type_t FIELD_HOPE_SERVER_CIPHER_MODE = {0x0E, 0xC3}; /* 3779 */
static const hl_field_type_t FIELD_HOPE_CLIENT_CIPHER_MODE = {0x0E, 0xC4}; /* 3780 */
static const hl_field_type_t FIELD_HOPE_SERVER_IV       = {0x0E, 0xC5}; /* 3781 */
static const hl_field_type_t FIELD_HOPE_CLIENT_IV       = {0x0E, 0xC6}; /* 3782 */
static const hl_field_type_t FIELD_HOPE_SERVER_CHECKSUM = {0x0E, 0xC7}; /* 3783 */
static const hl_field_type_t FIELD_HOPE_CLIENT_CHECKSUM = {0x0E, 0xC8}; /* 3784 */
static const hl_field_type_t FIELD_HOPE_SERVER_COMPRESS = {0x0E, 0xC9}; /* 3785 */
static const hl_field_type_t FIELD_HOPE_CLIENT_COMPRESS = {0x0E, 0xCA}; /* 3786 */

/* --- MAC algorithm identifiers --- */

typedef enum {
    HL_HOPE_MAC_HMAC_SHA256 = 0,
    HL_HOPE_MAC_HMAC_SHA1,
    HL_HOPE_MAC_SHA1,
    HL_HOPE_MAC_HMAC_MD5,
    HL_HOPE_MAC_MD5,
    HL_HOPE_MAC_INVERSE,
    HL_HOPE_MAC_COUNT      /* sentinel — number of algorithms */
} hl_hope_mac_alg_t;

/* --- RC4 stream cipher state (ARC4) --- */

typedef struct {
    uint8_t s[256];
    uint8_t i;
    uint8_t j;
} hl_rc4_t;

/* --- Per-direction cipher state --- */

typedef struct {
    hl_rc4_t  rc4;
    uint8_t   current_key[64];
    size_t    key_len;
} hl_hope_cipher_t;

/* --- Incremental decrypt phases --- */

typedef enum {
    HOPE_PHASE_HEADER,       /* waiting for / decrypting 20-byte header */
    HOPE_PHASE_BODY_PREFIX,  /* decrypting first 2 bytes of body */
    HOPE_PHASE_BODY_REST     /* decrypting remaining body bytes */
} hl_hope_decrypt_phase_t;

/* --- AEAD (ChaCha20-Poly1305) cipher state --- */

typedef struct {
    uint8_t  encode_key[32];   /* server -> client (HKDF-expanded) */
    uint8_t  decode_key[32];   /* client -> server (HKDF-expanded) */
    uint64_t send_counter;     /* nonce counter for outbound frames */
    uint64_t recv_counter;     /* nonce counter for inbound frames */
    uint8_t  ft_base_key[32];  /* file transfer base key */
    int      ft_base_key_set;  /* 1 if ft_base_key is valid */
} hl_hope_aead_state_t;

/* --- Per-connection HOPE state --- */

typedef struct hl_hope_state {
    int                     active;          /* 1 = RC4 transport encryption on */
    int                     aead_active;     /* 1 = AEAD transport encryption on */
    hl_hope_mac_alg_t       mac_alg;         /* negotiated MAC algorithm */
    uint8_t                 session_key[64]; /* per-connection session key */

    /* RC4 stream cipher state */
    hl_hope_cipher_t        encrypt;         /* server -> client */
    hl_hope_cipher_t        decrypt;         /* client -> server */

    /* Incremental decryption state machine for kqueue partial reads (RC4) */
    hl_hope_decrypt_phase_t decrypt_phase;
    size_t                  decrypt_offset;     /* bytes in read_buf already decrypted */
    size_t                  current_body_len;   /* body len from current transaction header */
    size_t                  body_rest_remaining; /* bytes left to decrypt in BODY_REST phase */
    uint8_t                 rotation_count;     /* extracted from current header */

    /* AEAD (ChaCha20-Poly1305) state */
    hl_hope_aead_state_t    aead;
} hl_hope_state_t;

/* --- RC4 primitives --- */

/* Initialize RC4 state from key. */
void hl_rc4_init(hl_rc4_t *rc4, const uint8_t *key, size_t key_len);

/* XOR data in-place with the RC4 keystream. */
void hl_rc4_process(hl_rc4_t *rc4, uint8_t *data, size_t len);

/* --- MAC computation --- */

/*
 * Compute a MAC using the selected algorithm.
 *
 * For HMAC variants:  HMAC(key, data)
 * For bare hashes:    H(key || data)
 * For INVERSE:        bitwise NOT of key bytes (data ignored)
 *
 * Output is written to `out` (must hold at least 64 bytes).
 * Actual output length is written to *out_len.
 */
void hl_hope_mac(hl_hope_mac_alg_t alg,
                 const uint8_t *data, size_t data_len,
                 const uint8_t *key, size_t key_len,
                 uint8_t *out, size_t *out_len);

/* --- HKDF-SHA256 key derivation (RFC 5869) --- */

/*
 * HKDF-SHA256 extract-then-expand.
 *
 * Derives `out_len` bytes of key material from input key material (ikm)
 * using salt and info parameters per RFC 5869.
 *
 * out_len MUST be <= 32 (one HMAC-SHA256 block). For HOPE AEAD key
 * derivation, out_len is always 32.
 */
void hl_hkdf_sha256(const uint8_t *ikm, size_t ikm_len,
                    const uint8_t *salt, size_t salt_len,
                    const uint8_t *info, size_t info_len,
                    uint8_t *out, size_t out_len);

/* --- Cipher policy --- */

typedef enum {
    HL_HOPE_CIPHER_PREFER_AEAD,  /* default: AEAD if client supports, else RC4 */
    HL_HOPE_CIPHER_REQUIRE_AEAD, /* reject RC4-only HOPE clients */
    HL_HOPE_CIPHER_RC4_ONLY      /* existing behavior, for testing */
} hl_hope_cipher_policy_t;

/* Negotiated cipher mode (returned by hl_hope_build_negotiation_reply) */
typedef enum {
    HL_HOPE_CIPHER_MODE_NONE,    /* no transport encryption (INVERSE auth-only) */
    HL_HOPE_CIPHER_MODE_RC4,     /* RC4 stream encryption */
    HL_HOPE_CIPHER_MODE_AEAD     /* ChaCha20-Poly1305 AEAD */
} hl_hope_cipher_mode_t;

/* Parse cipher policy from config string. Returns PREFER_AEAD for unknown values. */
hl_hope_cipher_policy_t hl_hope_parse_cipher_policy(const char *str);

/* --- HOPE security policy --- */

/*
 * Check if an algorithm is allowed under the given security policy.
 *
 * Strict mode (legacy_mode=0): only HMAC-SHA256, HMAC-SHA1, and HMAC-MD5 are allowed.
 *   Rejects INVERSE (auth bypass), bare SHA1 (length-extension), bare MD5.
 * Legacy mode (legacy_mode=1): all algorithms allowed per original spec.
 *
 * Returns 1 if allowed, 0 if rejected.
 */
int hl_hope_algorithm_allowed(hl_hope_mac_alg_t alg, int legacy_mode);

/* --- HOPE protocol functions --- */

/*
 * Detect whether a login transaction is a HOPE identification probe.
 * Returns 1 if UserLogin field contains exactly one byte 0x00.
 */
int hl_hope_detect_probe(const hl_transaction_t *login_tran);

/*
 * Build the server's HOPE negotiation reply.
 *
 * Parses the client's algorithm proposals from `probe`, selects the
 * strongest mutual MAC algorithm, generates a 64-byte session key
 * (IP + port + random), and populates `reply` with HOPE fields.
 *
 * cipher_policy controls whether CHACHA20-POLY1305 AEAD is offered:
 *   PREFER_AEAD: use AEAD if client supports, else RC4
 *   REQUIRE_AEAD: reject if client doesn't support AEAD
 *   RC4_ONLY: ignore AEAD in client's cipher list
 *
 * On success, *out_cipher_mode is set to the negotiated cipher mode.
 * `state` is populated with session_key and mac_alg.
 *
 * Returns 0 on success, -1 on error or negotiation rejection.
 */
int hl_hope_build_negotiation_reply(hl_hope_state_t *state,
                                    const hl_transaction_t *probe,
                                    hl_transaction_t *reply,
                                    const char *server_ip,
                                    uint16_t server_port,
                                    int legacy_mode,
                                    hl_hope_cipher_policy_t cipher_policy,
                                    hl_hope_cipher_mode_t *out_cipher_mode);

/*
 * Verify HOPE-authenticated login credentials.
 *
 * Iterates all accounts via acct_mgr, computes MAC(login, session_key)
 * for each, and matches against f_login->data. On login match, verifies
 * MAC(password, session_key) against f_password->data.
 *
 * Accounts with sha1: hashed passwords are skipped (HOPE requires
 * plaintext-stored passwords).
 *
 * On success, *out_acct points to the matched account. Returns 1 on
 * success, 0 on failure.
 */
int hl_hope_verify_login(hl_hope_state_t *state,
                         const hl_field_t *f_login,
                         const hl_field_t *f_password,
                         hl_account_mgr_t *acct_mgr,
                         hl_account_t **out_acct);

/*
 * Derive transport encryption keys and initialize RC4 ciphers.
 *
 * Key derivation per spec:
 *   password_mac = MAC(password, session_key)
 *   encode_key   = MAC(password, password_mac)   (server -> client)
 *   decode_key   = MAC(password, encode_key)     (client -> server)
 *
 * For INVERSE MAC, no transport encryption is possible (auth-only).
 * Returns 0 on success, -1 on error.
 */
int hl_hope_derive_keys(hl_hope_state_t *state, const char *password);

/*
 * Derive AEAD transport encryption keys and file transfer base key.
 *
 * Key derivation:
 *   password_mac = MAC(password, session_key)
 *   encode_key   = MAC(password, password_mac)
 *   decode_key   = MAC(password, encode_key)
 *   encode_key_256 = HKDF-SHA256(encode_key, session_key, "hope-chacha-encode")
 *   decode_key_256 = HKDF-SHA256(decode_key, session_key, "hope-chacha-decode")
 *   ft_base_key    = HKDF-SHA256(encode_key_256 || decode_key_256, session_key, "hope-file-transfer")
 *
 * Sets state->aead with the expanded keys and zeroed counters.
 * Returns 0 on success, -1 on error (e.g. INVERSE MAC).
 */
int hl_hope_aead_derive_keys(hl_hope_state_t *state, const char *password);

/*
 * Derive a per-transfer key for AEAD-encrypted HTXF file transfers.
 *
 * transfer_key = HKDF-SHA256(ft_base_key, ref_number(4 bytes BE), "hope-ft-ref")
 *
 * ref_num is the 4-byte big-endian HTXF reference number.
 * Writes 32 bytes to out_key. Returns 0 on success, -1 if ft_base_key not set.
 */
int hl_hope_aead_derive_transfer_key(const hl_hope_state_t *state,
                                     const uint8_t ref_num[4],
                                     uint8_t out_key[32]);

/* --- AEAD transport functions --- */

/*
 * Encrypt a serialized transaction into an AEAD frame.
 *
 * Input: `tx_buf` contains a serialized transaction (header + body), `tx_len` bytes.
 * Output: `out_buf` receives the AEAD frame: 4-byte BE length + ciphertext + 16-byte tag.
 * `out_buf` must hold at least `tx_len + 4 + 16` bytes.
 *
 * Returns total bytes written to out_buf, or -1 on error.
 * Increments state->aead.send_counter.
 */
int hl_hope_aead_encrypt_transaction(hl_hope_state_t *state,
                                     const uint8_t *tx_buf, size_t tx_len,
                                     uint8_t *out_buf, size_t out_buf_size);

/*
 * Scan read_buf for a complete AEAD frame.
 *
 * Returns the total frame size (4 + ciphertext_len) if a complete frame
 * is available starting at buf[0], or 0 if more data is needed.
 * Returns -1 if the frame length exceeds max_frame_size.
 */
int hl_hope_aead_scan_frame(const uint8_t *buf, size_t buf_len,
                            size_t max_frame_size);

/*
 * Decrypt an AEAD frame in-place.
 *
 * `buf` points to the start of a complete AEAD frame (4-byte length prefix +
 * ciphertext + tag). `frame_len` is the total frame size (from scan_frame).
 *
 * On success, writes decrypted plaintext to `out_buf` (must hold frame_len - 4 - 16 bytes),
 * sets *out_len to the plaintext length, and increments state->aead.recv_counter.
 * Returns 0 on success, -1 on tag verification failure.
 */
int hl_hope_aead_decrypt_frame(hl_hope_state_t *state,
                               const uint8_t *buf, size_t frame_len,
                               uint8_t *out_buf, size_t *out_len);

/* --- AEAD stream reader for file transfers --- */

/*
 * Buffered AEAD stream reader for file transfer uploads.
 *
 * Reads AEAD frames from the underlying connection, decrypts them,
 * and buffers the plaintext so callers can read arbitrary byte ranges
 * as if reading from a plain stream.
 */
typedef struct {
    struct hl_tls_conn *conn;       /* underlying transfer connection */
    uint8_t             key[32];    /* per-transfer AEAD key */
    uint64_t            recv_counter;
    uint8_t            *buffer;     /* decrypted plaintext buffer */
    size_t              buffer_len; /* bytes in buffer */
    size_t              buffer_off; /* read cursor offset */
} hl_aead_stream_reader_t;

/* Initialize an AEAD stream reader. Does not allocate — caller provides the struct. */
void hl_aead_reader_init(hl_aead_stream_reader_t *r,
                         struct hl_tls_conn *conn,
                         const uint8_t key[32]);

/* Free internal buffer. Does NOT close the connection. */
void hl_aead_reader_free(hl_aead_stream_reader_t *r);

/*
 * Read exactly `len` bytes from the AEAD stream.
 * Reads and decrypts AEAD frames as needed to fill the request.
 * Returns 0 on success, -1 on error (connection closed, tag failure, etc.)
 */
int hl_aead_reader_read_full(hl_aead_stream_reader_t *r,
                             uint8_t *out, size_t len);

/*
 * Read up to `len` bytes from the AEAD stream.
 * Returns bytes read (>0), 0 on EOF, -1 on error.
 */
ssize_t hl_aead_reader_read(hl_aead_stream_reader_t *r,
                            uint8_t *out, size_t len);

/*
 * Encrypt an outbound transaction buffer in-place (RC4).
 *
 * buf must contain a serialized transaction (header + body).
 * Encrypts header (20 bytes), then body, using the encrypt cipher.
 * Rotation count is set to 0 (no outbound rotation for now).
 */
void hl_hope_encrypt_transaction(hl_hope_state_t *state,
                                 uint8_t *buf, size_t len);

/*
 * Incrementally decrypt inbound data in the client's read buffer.
 *
 * Uses the three-phase state machine (HEADER -> BODY_PREFIX -> BODY_REST)
 * to handle partial reads from the kqueue event loop.
 *
 * buf is the client's read_buf, buf_len is read_buf_len.
 * After return, state->decrypt_offset indicates how many bytes
 * from the start of buf are now plaintext.
 *
 * Returns the new decrypt_offset value.
 */
size_t hl_hope_decrypt_incremental(hl_hope_state_t *state,
                                   uint8_t *buf, size_t buf_len);

/*
 * Adjust decrypt_offset after consuming bytes from read_buf.
 * Call this after memmove()-ing consumed transaction bytes.
 * `consumed` is the number of bytes removed from the front of buf.
 */
void hl_hope_adjust_offset(hl_hope_state_t *state, size_t consumed);

/* Free any resources held by hope state (currently none, but future-proof). */
void hl_hope_state_free(hl_hope_state_t *state);

/* --- Algorithm list encoding/decoding --- */

/*
 * Parse an algorithm list from HOPE wire format and select the strongest
 * mutually acceptable algorithm.
 *
 * In strict mode (legacy_mode=0), only HMAC-SHA1 and HMAC-MD5 are accepted.
 * Returns (hl_hope_mac_alg_t)-1 if no acceptable algorithm is found.
 *
 * In legacy mode (legacy_mode=1), falls back to HL_HOPE_MAC_INVERSE.
 */
hl_hope_mac_alg_t hl_hope_select_best_algorithm(const uint8_t *data,
                                                  size_t data_len,
                                                  int legacy_mode);

/*
 * Encode a single algorithm selection into HOPE wire format.
 * Format: <u16:1> <u8:len> <str:name>
 * Returns bytes written to buf, or -1 if buf too small.
 */
int hl_hope_encode_algorithm_selection(hl_hope_mac_alg_t alg,
                                       uint8_t *buf, size_t buf_len);

/*
 * Encode a single cipher selection into HOPE wire format.
 * Returns bytes written to buf, or -1 if buf too small.
 */
int hl_hope_encode_cipher_selection(const char *name,
                                    uint8_t *buf, size_t buf_len);

/* --- Encryption-gated content access --- */

/*
 * Returns 1 if this client has active HOPE transport encryption
 * (not INVERSE auth-only). Returns 0 otherwise.
 */
int hl_client_is_encrypted(const struct hl_client_conn *cc);

/*
 * Returns 1 if a name (file/folder/category) starts with the E2E prefix.
 * name_len is the byte length of name (may not be NUL-terminated).
 * If prefix is empty or NULL, always returns 0.
 */
int hl_hope_name_requires_encryption(const char *name, size_t name_len,
                                     const char *prefix);

/*
 * Returns 1 if any path component after file_root starts with the prefix.
 * full_path and file_root must be resolved (realpath'd) absolute paths.
 */
int hl_hope_path_requires_encryption(const char *full_path,
                                     const char *file_root,
                                     const char *prefix);

#endif /* HOTLINE_HOPE_H */
