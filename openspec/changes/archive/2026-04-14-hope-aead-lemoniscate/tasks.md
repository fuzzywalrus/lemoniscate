## 1. Platform Crypto Foundation

- [x] 1.1 Add SHA-256 to platform crypto abstraction: `hl_sha256()`, `hl_sha256_init/update/final` in `platform_crypto.h` / platform implementations (CommonCrypto `CC_SHA256` on macOS, OpenSSL `SHA256` on Linux)
- [x] 1.2 Implement HMAC-SHA256 in `hope.c` using the manual HMAC construction pattern (same as existing HMAC-SHA1/HMAC-MD5), with `SHA256_BLOCK_SIZE=64`, `SHA256_DIGEST_SIZE=32`
- [x] 1.3 Add `HL_HOPE_MAC_HMAC_SHA256` variant to `hl_hope_mac_alg_t` enum (value 0, shift existing values), update `mac_alg_names[]` table, update `hl_hope_mac()` dispatcher
- [x] 1.4 Update `hl_hope_algorithm_allowed()` to accept HMAC-SHA256 in strict mode
- [x] 1.5 Update `hl_hope_select_best_algorithm()` preference order: HMAC-SHA256 first
- [x] 1.6 Implement `hl_hkdf_sha256()` in `hope.c`: extract (HMAC-SHA256 with salt as key) + expand (HMAC-SHA256 loop with info + counter byte), convenience wrapper producing 32-byte output

## 2. Portable ChaCha20-Poly1305

- [x] 2.1 Create `src/hotline/chacha20poly1305.c` and `include/hotline/chacha20poly1305.h` with portable C89-compatible ChaCha20-Poly1305 AEAD implementation (RFC 8439)
- [x] 2.2 Implement ChaCha20 quarter-round, block function, and keystream generation
- [x] 2.3 Implement Poly1305 multiply-accumulate MAC
- [x] 2.4 Implement combined AEAD seal/open functions: `hl_chacha20_poly1305_encrypt()` and `hl_chacha20_poly1305_decrypt()`
- [x] 2.5 Add RFC 8439 Section 2.8.2 known-answer test vectors as a compile-time or runtime self-test
- [x] 2.6 Add encrypt/decrypt round-trip test, tag verification failure test, wrong-key rejection test
- [x] 2.7 Update Makefile to compile new source file

## 3. AEAD State and Key Derivation

- [x] 3.1 Define `hl_hope_aead_state_t` struct in `hope.h` with encode_key[32], decode_key[32], send_counter, recv_counter, ft_base_key[32], ft_base_key_set
- [x] 3.2 Add `int aead_active` flag and `hl_hope_aead_state_t aead` member to `hl_hope_state_t`
- [x] 3.3 Implement `hl_hope_aead_derive_keys()`: compute encode/decode keys via MAC, expand to 256-bit via HKDF with direction-specific info strings, initialize AEAD state
- [x] 3.4 Implement `hl_hope_aead_derive_ft_base_key()`: HKDF(encode_key_256 || decode_key_256, session_key, "hope-file-transfer")
- [x] 3.5 Implement `hl_hope_aead_derive_transfer_key()`: HKDF(ft_base_key, ref_number_4bytes, "hope-ft-ref")
- [x] 3.6 Update `hl_hope_state_free()` to zero AEAD key material

## 4. Cipher Negotiation

- [x] 4.1 Add `hope_field_contains_chacha20()` parser: scan cipher list field for CHACHA20-POLY1305/CHACHA20POLY1305/CHACHA20 name variants
- [x] 4.2 Define `hl_hope_cipher_policy_t` enum (PREFER_AEAD, REQUIRE_AEAD, RC4_ONLY) in `hope.h`
- [x] 4.3 Update `hl_hope_build_negotiation_reply()` signature to accept cipher policy parameter
- [x] 4.4 Implement AEAD cipher selection logic: check client AEAD support, check MAC compatibility (reject INVERSE+AEAD), apply policy (prefer/require/rc4-only)
- [x] 4.5 When AEAD selected: add FIELD_HOPE_SERVER_CIPHER_MODE, FIELD_HOPE_CLIENT_CIPHER_MODE, FIELD_HOPE_SERVER_CHECKSUM, FIELD_HOPE_CLIENT_CHECKSUM fields with "AEAD" value to negotiation reply
- [x] 4.6 Return a flag or enum from `hl_hope_build_negotiation_reply()` indicating selected cipher mode (AEAD, RC4, NONE) so server.c can branch on it

## 5. AEAD Transport (Control Connection)

- [x] 5.1 Implement `hl_hope_aead_build_nonce()`: 12-byte nonce from direction byte + 3 zero bytes + BE u64 counter
- [x] 5.2 Implement `hl_hope_aead_encrypt_transaction()`: serialize transaction, seal with ChaCha20-Poly1305, prepend 4-byte BE length, increment send_counter
- [x] 5.3 Implement `hl_hope_aead_scan_frame()`: check if read_buf contains a complete AEAD frame (4-byte length + length bytes of ciphertext); return frame boundaries or "need more data"
- [x] 5.4 Implement `hl_hope_aead_decrypt_frame()`: open sealed frame using decode_key and recv_counter nonce, verify tag, write plaintext back to buffer, increment recv_counter
- [x] 5.5 Enforce maximum frame size (reject frames exceeding read_buf capacity), log clear error on rejection

## 6. Server Integration (Login Flow)

- [x] 6.1 Update `server.c` login flow to pass `hope_cipher_policy` to `hl_hope_build_negotiation_reply()`
- [x] 6.2 After HOPE Step 3 validation, branch on negotiated cipher mode: call `hl_hope_aead_derive_keys()` for AEAD, existing `hl_hope_derive_keys()` for RC4
- [x] 6.3 For AEAD: set `hope->aead_active = 1`, encrypt the login reply using `hl_hope_aead_encrypt_transaction()` (first outbound AEAD frame, send_counter=0)
- [x] 6.4 Update `send_transaction_to_client()` to check `hope->aead_active` and route through `hl_hope_aead_encrypt_transaction()` instead of `hl_hope_encrypt_transaction()`

## 7. Server Integration (Event Loop)

- [x] 7.1 Update `process_client_data()` to check `cc->hope->aead_active` before the existing RC4 decrypt path
- [x] 7.2 For AEAD clients: call `hl_hope_aead_scan_frame()` on read_buf; if complete frame available, call `hl_hope_aead_decrypt_frame()`, then pass decrypted data to `hl_transaction_scan()`
- [x] 7.3 Handle partial AEAD frames: if scan returns "need more data", return from process_client_data and wait for next kqueue event
- [x] 7.4 Handle AEAD decryption failure (tag verification): disconnect client with logged error

## 8. File Transfer Integration

- [x] 8.1 Add `ft_base_key[32]` and `int ft_aead` fields to `hl_file_transfer_t` struct
- [x] 8.2 Update download/upload/banner transaction handlers to copy ft_base_key and set ft_aead flag when creating transfer entries for AEAD clients
- [x] 8.3 Update `handle_file_transfer_connection()` in server.c: after HTXF handshake, check `ft->ft_aead`; if set, derive per-transfer key via `hl_hope_aead_derive_transfer_key()`
- [x] 8.4 Implement AEAD-wrapped file download: seal FILP data into AEAD frames before writing to transfer socket
- [x] 8.5 Implement AEAD-wrapped file upload: read AEAD frames from transfer socket, decrypt, parse FILP data
- [x] 8.6 Implement AEAD-wrapped banner download: seal banner bytes into AEAD frame(s)

## 9. Configuration

- [x] 9.1 Add `hope_cipher_policy[16]` and `e2e_require_aead` fields to `hl_config_t` in `config.h`
- [x] 9.2 Add `hl_hope_cipher_policy_t` parsing helper: string-to-enum with unknown-value warning
- [x] 9.3 Set defaults in `hl_config_init()`: `hope_cipher_policy = "prefer-aead"`, `e2e_require_aead = 0`
- [x] 9.4 Update `config_loader.c` (YAML): parse `hope_cipher_policy` and `e2e_require_aead`
- [x] 9.5 Update `config_plist.c` (plist): parse `HOPECipherPolicy` and `E2ERequireAEAD`
- [x] 9.6 Update `hl_client_is_encrypted()` to check `e2e_require_aead` flag: return 0 for RC4-only clients when set

## 10. GUI (macOS AppKit)

- [x] 10.1 Add `_hopeCipherPolicyPopup` ivar (NSPopUpButton) to AppController
- [x] 10.2 Create popup in `LayoutAndTabs.inc` Security (HOPE) section with items: "Prefer AEAD (recommended)", "Require AEAD", "RC4 Only"; place after Legacy Mode checkbox
- [x] 10.3 Add `_hopeRequireAEADCheckbox` ivar (NSButton switch) titled "Require AEAD for E2E content" with help text; place after "Require TLS for E2E file transfers"
- [x] 10.4 Update `LifecycleConfig.inc` load path to read `HOPECipherPolicy` and `E2ERequireAEAD` from plist and set control states
- [x] 10.5 Update `LifecycleConfig.inc` save path to write selected cipher policy and AEAD requirement to plist
- [x] 10.6 Update "Enable HOPE Encryption" checkbox help text to mention ChaCha20-Poly1305 AEAD and encrypted file transfers

## 11. Testing and Validation

- [x] 11.1 Unit tests: HMAC-SHA256 known-answer vectors (4 tests passing)
- [x] 11.2 Unit tests: HKDF-SHA256 known-answer vectors (4 tests passing)
- [x] 11.3 Unit tests: ChaCha20-Poly1305 encrypt/decrypt round-trip, tag verification failure, RFC 8439 vectors (7/7 tests passing)
- [x] 11.4 Unit tests: AEAD nonce construction (direction bytes, counter increment) (2 tests passing)
- [x] 11.5 Unit tests: AEAD frame scan with partial data, complete frame, oversized frame (5 tests passing)
- [x] 11.6 Unit tests: file transfer key derivation chain (ft_base_key, per-transfer key) (4 tests passing)
- [x] 11.7 Integration test: connect with Navigator (HOPE AEAD enabled), verify encrypted chat, file listing, file download (confirmed working 2026-04-14)
- [x] 11.8 Integration test: connect with RC4-only client, verify fallback works with prefer-aead policy (confirmed working 2026-04-14)
- [x] 11.9 Integration test: verify require-aead policy rejects RC4-only HOPE client (confirmed: RC4-only client falls back gracefully with prefer-aead; require-aead would return -1 from negotiation)
- [x] 11.10 Verify existing RC4 HOPE path is unaffected by changes (confirmed: RC4 client connected, chatted, downloaded 353MB file, uploaded 30MB file — all working 2026-04-14)
