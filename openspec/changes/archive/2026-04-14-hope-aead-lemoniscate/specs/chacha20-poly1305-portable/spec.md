## ADDED Requirements

### Requirement: Portable ChaCha20-Poly1305 AEAD implementation

The system SHALL include a portable C implementation of ChaCha20-Poly1305 AEAD (RFC 8439) that compiles and runs correctly on all target platforms: macOS Tiger (10.4 PPC) through modern Apple Silicon, and Linux x86/ARM.

The implementation SHALL NOT depend on OpenSSL, CommonCrypto, or any external library for ChaCha20 or Poly1305 operations.

The implementation SHALL provide:
- `hl_chacha20_poly1305_encrypt(key, nonce, plaintext, plaintext_len, ciphertext_out, tag_out)` — seal plaintext, produce ciphertext + 16-byte tag
- `hl_chacha20_poly1305_decrypt(key, nonce, ciphertext, ciphertext_len, tag, plaintext_out)` — open ciphertext, verify tag, produce plaintext; return -1 on tag mismatch

Key size SHALL be 32 bytes. Nonce size SHALL be 12 bytes. Tag size SHALL be 16 bytes.

#### Scenario: Encrypt-decrypt round trip

- **WHEN** a plaintext message is sealed with a key and nonce, then opened with the same key and nonce
- **THEN** the decrypted output SHALL match the original plaintext exactly

#### Scenario: Tag verification failure on corrupted ciphertext

- **WHEN** a sealed message has its ciphertext modified before decryption
- **THEN** the decrypt function SHALL return -1 and SHALL NOT produce plaintext output

#### Scenario: Tag verification failure on wrong key

- **WHEN** a sealed message is decrypted with a different key than was used for encryption
- **THEN** the decrypt function SHALL return -1

#### Scenario: RFC 8439 test vectors

- **WHEN** the implementation is tested against the known-answer test vectors from RFC 8439 Section 2.8.2
- **THEN** the ciphertext and tag SHALL match the expected values exactly

#### Scenario: Compilation on Tiger PPC

- **WHEN** the source is compiled with GCC on macOS Tiger (10.4 PPC)
- **THEN** the code SHALL compile without errors using C89/C99 compatible syntax (no C11 or later features)

### Requirement: Portable HKDF-SHA256 implementation

The system SHALL implement HKDF-SHA256 (RFC 5869) using the platform's SHA-256 hash primitive via the existing `hl_sha256` abstraction.

The implementation SHALL provide:
- `hl_hkdf_sha256(ikm, ikm_len, salt, salt_len, info, info_len, out, out_len)` — extract-then-expand, producing `out_len` bytes of key material (max 32 bytes for this use case)

The HMAC-SHA256 used internally by HKDF SHALL reuse the same manual HMAC construction pattern already used for HMAC-SHA1 and HMAC-MD5 in `hope.c`.

#### Scenario: HKDF known-answer test

- **WHEN** the HKDF implementation is tested with the test vectors from RFC 5869 Appendix A
- **THEN** the output key material SHALL match the expected values

#### Scenario: Deterministic output

- **WHEN** HKDF is called twice with the same ikm, salt, and info
- **THEN** the output SHALL be identical

#### Scenario: Different info strings produce different keys

- **WHEN** HKDF is called with the same ikm and salt but different info strings
- **THEN** the outputs SHALL differ
