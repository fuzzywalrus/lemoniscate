## MODIFIED Requirements

### Requirement: Supported MAC algorithms

The system SHALL support the following MAC algorithms for HOPE, listed in preference order (strongest to weakest):

1. **HMAC-SHA256** -- HMAC using SHA-256 (output: 32 bytes)
2. **HMAC-SHA1** -- HMAC using SHA-1 (output: 20 bytes)
3. **SHA1** -- Bare `SHA1(key + message)` concatenation (output: 20 bytes)
4. **HMAC-MD5** -- HMAC using MD5 (output: 16 bytes)
5. **MD5** -- Bare `MD5(key + message)` concatenation (output: 16 bytes)
6. **INVERSE** -- Returns each byte of the key bitwise-NOT'd (ignores message; authentication-only, cannot derive transport keys or AEAD key material)

The server sends the selected algorithm back to the client. Algorithm names are case-insensitive on the wire.

In strict mode (legacy_mode=0), only HMAC-SHA256, HMAC-SHA1, and HMAC-MD5 are accepted.

#### Scenario: Server selects HMAC-SHA256

- **WHEN** the client's algorithm list includes `HMAC-SHA256` and it passes the security policy
- **THEN** the server SHALL select `HMAC-SHA256`, producing 32-byte MAC outputs

#### Scenario: Server selects HMAC-SHA1

- **WHEN** the client's algorithm list includes `HMAC-SHA1` but not `HMAC-SHA256`, and it passes the security policy
- **THEN** the server SHALL select `HMAC-SHA1`, producing 20-byte MAC outputs

#### Scenario: Server selects INVERSE

- **WHEN** the server is in legacy mode and the client's only mutually acceptable algorithm is `INVERSE`
- **THEN** the server SHALL select `INVERSE`; transport encryption SHALL NOT be activated (INVERSE does not support key derivation); AEAD mode SHALL NOT be activated

#### Scenario: HMAC-SHA256 in strict mode

- **WHEN** the server is in strict mode (legacy_mode=0) and the client offers `HMAC-SHA256`
- **THEN** `HMAC-SHA256` SHALL be accepted (it is a proper HMAC construction)

### Requirement: HOPE negotiation reply cipher fields

The HOPE negotiation reply SHALL include cipher-related fields based on the negotiated cipher mode.

When RC4 is negotiated (existing behavior):
- `FIELD_HOPE_SERVER_CIPHER`: `"RC4"` (encoded as cipher selection)
- `FIELD_HOPE_CLIENT_CIPHER`: `"RC4"` (encoded as cipher selection)

When CHACHA20-POLY1305 is negotiated:
- `FIELD_HOPE_SERVER_CIPHER`: `"CHACHA20-POLY1305"` (encoded as cipher selection)
- `FIELD_HOPE_CLIENT_CIPHER`: `"CHACHA20-POLY1305"` (encoded as cipher selection)
- `FIELD_HOPE_SERVER_CIPHER_MODE` (0x0EC3): `"AEAD"` (4 raw ASCII bytes)
- `FIELD_HOPE_CLIENT_CIPHER_MODE` (0x0EC4): `"AEAD"` (4 raw ASCII bytes)
- `FIELD_HOPE_SERVER_CHECKSUM` (0x0EC7): `"AEAD"` (4 raw ASCII bytes)
- `FIELD_HOPE_CLIENT_CHECKSUM` (0x0EC8): `"AEAD"` (4 raw ASCII bytes)

#### Scenario: RC4 negotiation reply (unchanged)

- **WHEN** the server selects RC4 as the transport cipher
- **THEN** the reply SHALL contain `FIELD_HOPE_SERVER_CIPHER` and `FIELD_HOPE_CLIENT_CIPHER` with value `"RC4"` and SHALL NOT contain CipherMode or Checksum fields

#### Scenario: AEAD negotiation reply

- **WHEN** the server selects CHACHA20-POLY1305 as the transport cipher
- **THEN** the reply SHALL contain all six fields: ServerCipher, ClientCipher (both `"CHACHA20-POLY1305"`), ServerCipherMode, ClientCipherMode, ServerChecksum, ClientChecksum (all `"AEAD"`)

### Requirement: Encryption-gated content access

The `hl_client_is_encrypted()` function SHALL return 1 only when the client has active HOPE transport encryption that meets the server's configured requirements.

When `e2e_require_aead` is false (default): any active HOPE transport (RC4 or AEAD) that is not INVERSE qualifies.

When `e2e_require_aead` is true: only active AEAD transport qualifies. RC4-only HOPE clients SHALL be treated as unencrypted for E2E access purposes.

The existing `e2e_require_tls` check continues to apply independently.

#### Scenario: AEAD client with e2e_require_aead enabled

- **WHEN** `e2e_require_aead` is true and the client has AEAD HOPE transport active
- **THEN** `hl_client_is_encrypted()` SHALL return 1

#### Scenario: RC4 client with e2e_require_aead enabled

- **WHEN** `e2e_require_aead` is true and the client has RC4 HOPE transport active (not AEAD)
- **THEN** `hl_client_is_encrypted()` SHALL return 0

#### Scenario: RC4 client with e2e_require_aead disabled

- **WHEN** `e2e_require_aead` is false and the client has RC4 HOPE transport active
- **THEN** `hl_client_is_encrypted()` SHALL return 1 (existing behavior preserved)
