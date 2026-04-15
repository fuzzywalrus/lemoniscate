## ADDED Requirements

### Requirement: ChaCha20-Poly1305 AEAD cipher negotiation

The server SHALL advertise support for `CHACHA20-POLY1305` as a negotiable cipher during HOPE Step 2 (negotiation reply) when the client's Step 1 identification includes `CHACHA20-POLY1305` in its `HopeClientCipher` or `HopeServerCipher` fields.

When selecting CHACHA20-POLY1305, the server's negotiation reply SHALL include:
- `FIELD_HOPE_SERVER_CIPHER` (0x0EC1): `"CHACHA20-POLY1305"` (encoded as cipher selection)
- `FIELD_HOPE_CLIENT_CIPHER` (0x0EC2): `"CHACHA20-POLY1305"` (encoded as cipher selection)
- `FIELD_HOPE_SERVER_CIPHER_MODE` (0x0EC3): `"AEAD"` (4 raw ASCII bytes)
- `FIELD_HOPE_CLIENT_CIPHER_MODE` (0x0EC4): `"AEAD"` (4 raw ASCII bytes)
- `FIELD_HOPE_SERVER_CHECKSUM` (0x0EC7): `"AEAD"` (4 raw ASCII bytes)
- `FIELD_HOPE_CLIENT_CHECKSUM` (0x0EC8): `"AEAD"` (4 raw ASCII bytes)

The server SHALL normalize cipher name variants: `CHACHA20POLY1305` and `CHACHA20` SHALL be treated as `CHACHA20-POLY1305`.

#### Scenario: Client supports ChaCha20-Poly1305 with prefer-aead policy

- **WHEN** the client's HOPE identification includes `CHACHA20-POLY1305` in its cipher list, the selected MAC algorithm is not INVERSE, and the server's cipher policy is `prefer-aead`
- **THEN** the server SHALL select `CHACHA20-POLY1305` and include all six AEAD-related fields in the negotiation reply

#### Scenario: Client supports only RC4 with prefer-aead policy

- **WHEN** the client's HOPE identification includes `RC4` but not `CHACHA20-POLY1305`, and the server's cipher policy is `prefer-aead`
- **THEN** the server SHALL fall back to RC4 and include only `FIELD_HOPE_SERVER_CIPHER` and `FIELD_HOPE_CLIENT_CIPHER` with value `"RC4"` (no CipherMode or Checksum fields)

#### Scenario: Client supports only RC4 with require-aead policy

- **WHEN** the client's HOPE identification includes only `RC4` and the server's cipher policy is `require-aead`
- **THEN** the server SHALL reject the HOPE negotiation (return -1 from `hl_hope_build_negotiation_reply`)

#### Scenario: Cipher name normalization

- **WHEN** the client sends a cipher name of `"CHACHA20POLY1305"` or `"CHACHA20"`
- **THEN** the server SHALL normalize it to `"CHACHA20-POLY1305"` and proceed with AEAD selection

#### Scenario: INVERSE MAC with AEAD

- **WHEN** the selected MAC algorithm is INVERSE and the client supports CHACHA20-POLY1305
- **THEN** the server SHALL NOT select AEAD (INVERSE cannot produce cryptographic key material for HKDF) and SHALL fall back to RC4 if available, or reject if cipher policy is `require-aead`

### Requirement: HMAC-SHA256 MAC algorithm

The server SHALL support `HMAC-SHA256` as a MAC algorithm for HOPE. It SHALL be the highest-preference algorithm in the selection order, before HMAC-SHA1.

HMAC-SHA256 produces 32-byte outputs, which natively match the ChaCha20-Poly1305 key size without truncation.

The wire name SHALL be `"HMAC-SHA256"` (case-insensitive parsing).

#### Scenario: Client offers HMAC-SHA256

- **WHEN** the client's MAC algorithm list includes `HMAC-SHA256` and it passes the server's security policy
- **THEN** the server SHALL select `HMAC-SHA256` as the negotiated MAC algorithm

#### Scenario: HMAC-SHA256 in strict mode

- **WHEN** the server is in strict mode (legacy_mode=0) and the client offers HMAC-SHA256
- **THEN** `HMAC-SHA256` SHALL be accepted (it is a proper HMAC construction)

#### Scenario: HMAC-SHA256 with RC4 transport

- **WHEN** the server selects HMAC-SHA256 as MAC but RC4 as cipher
- **THEN** the server SHALL use HMAC-SHA256 for all MAC computations and RC4 for transport (HMAC-SHA256 is not exclusive to AEAD)

### Requirement: HKDF-SHA256 key expansion for AEAD

When ChaCha20-Poly1305 is negotiated, the server SHALL expand MAC-derived keys to 256-bit (32-byte) keys using HKDF-SHA256 (RFC 5869).

Key derivation after HOPE Step 3:
1. `password_mac = MAC(key=password, msg=session_key)`
2. `encode_key = MAC(key=password, msg=password_mac)`
3. `decode_key = MAC(key=password, msg=encode_key)`
4. `encode_key_256 = HKDF-SHA256(ikm=encode_key, salt=session_key, info="hope-chacha-encode")`
5. `decode_key_256 = HKDF-SHA256(ikm=decode_key, salt=session_key, info="hope-chacha-decode")`

The server uses `encode_key_256` for its outbound traffic (server->client) and `decode_key_256` to decrypt inbound traffic (client->server).

#### Scenario: HKDF key expansion with HMAC-SHA256

- **WHEN** AEAD mode is negotiated with HMAC-SHA256 as the MAC
- **THEN** the server SHALL derive 32-byte encode and decode keys using HKDF-SHA256 with `session_key` as salt and direction-specific info strings

#### Scenario: HKDF key expansion with HMAC-SHA1

- **WHEN** AEAD mode is negotiated with HMAC-SHA1 as the MAC (producing 20-byte keys)
- **THEN** the server SHALL expand the 20-byte MAC outputs to 32-byte keys using HKDF-SHA256

### Requirement: AEAD framed transport

When ChaCha20-Poly1305 is the negotiated cipher, the server SHALL use length-prefixed AEAD frames for all transport after encryption is activated.

Frame structure:
```
+-------------------+-------------------------------+
| Length (4 bytes)   | Ciphertext + Tag             |
| big-endian uint32  | (variable + 16 bytes)        |
+-------------------+-------------------------------+
```

- The length field is a 4-byte big-endian unsigned integer encoding the size of the ciphertext including the 16-byte Poly1305 tag
- The length field itself is NOT encrypted and NOT authenticated
- Each Hotline transaction (header + body) SHALL be sealed as a single AEAD frame

The server SHALL enforce a maximum frame size. Frames exceeding this limit SHALL cause the connection to be closed.

#### Scenario: Encrypt and send a transaction (server outbound)

- **WHEN** a transaction is sent to an AEAD-mode client
- **THEN** the server SHALL serialize the transaction (header + body), seal it with ChaCha20-Poly1305 using `encode_key_256` producing ciphertext + 16-byte tag, write the 4-byte big-endian length prefix, then write the sealed ciphertext, and increment the send counter

#### Scenario: Receive and decrypt a transaction (server inbound)

- **WHEN** enough data has been buffered from the client to contain a complete AEAD frame (4-byte length + ciphertext + tag)
- **THEN** the server SHALL read the 4-byte length, open (decrypt + verify) the sealed data using `decode_key_256`, and parse the resulting plaintext as a Hotline transaction, and increment the receive counter

#### Scenario: Partial frame buffering

- **WHEN** a kqueue/epoll read event delivers fewer bytes than needed for a complete AEAD frame
- **THEN** the server SHALL buffer the partial data and wait for subsequent read events until the full frame is available before attempting decryption

#### Scenario: Authentication tag verification failure

- **WHEN** a received frame fails Poly1305 tag verification
- **THEN** the server SHALL close the connection and log a decryption error

#### Scenario: Oversized frame rejected

- **WHEN** a received frame has a length field exceeding the maximum allowed size
- **THEN** the server SHALL close the connection and log a protocol error

### Requirement: Deterministic nonce construction

The server SHALL construct 12-byte nonces for ChaCha20-Poly1305 using the following structure:

```
Byte:  0        1-3      4-11
      +--------+--------+------------------+
      |  dir   | 0x0000 | counter (BE u64) |
      +--------+--------+------------------+
```

- `dir`: `0x00` for server-to-client, `0x01` for client-to-server
- Bytes 1-3: zero padding
- Bytes 4-11: big-endian 64-bit unsigned counter, incrementing from 0

Each direction SHALL maintain its own counter. The direction byte prevents nonce reuse even if counters align.

#### Scenario: First server-to-client frame

- **WHEN** the server sends its first AEAD frame after encryption activation
- **THEN** the nonce SHALL be `[0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]`

#### Scenario: Counter increment

- **WHEN** the server sends its second AEAD frame
- **THEN** the send counter SHALL increment to 1 and the nonce SHALL be `[0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01]`

#### Scenario: Inbound nonce uses client direction

- **WHEN** the server decrypts the first frame from the client
- **THEN** the nonce SHALL use direction byte `0x01`: `[0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]`

### Requirement: AEAD-encrypted HTXF file transfers

When the control connection uses AEAD mode, HTXF file transfers SHALL also use ChaCha20-Poly1305 encryption.

Base key derivation:
```
ft_base_key = HKDF-SHA256(
    ikm  = encode_key_256 || decode_key_256,  (64 bytes)
    salt = session_key,                        (64 bytes)
    info = "hope-file-transfer"
)
```

Per-transfer key derivation:
```
transfer_key = HKDF-SHA256(
    ikm  = ft_base_key,
    salt = ref_number_bytes,     (4 bytes, big-endian)
    info = "hope-ft-ref"
)
```

The HTXF handshake (magic bytes "HTXF", reference number, transfer size) SHALL remain in plaintext. AEAD framing SHALL begin immediately after the handshake is validated. Subsequent data (FILP headers, fork data) SHALL use the same AEAD frame structure and nonce construction as control connections, with direction bytes following the same convention (server-to-client = 0x00 for downloads, client-to-server = 0x01 for uploads).

The `ft_base_key` SHALL be stored in the HOPE state after login. The per-transfer key SHALL be derived when the file transfer connection is matched to its transfer entry.

#### Scenario: Encrypted file download

- **WHEN** a file download is initiated by a client with active AEAD transport
- **THEN** the server SHALL read the HTXF handshake in plaintext, derive the transfer key from the reference number, and encrypt the outgoing FILP stream using AEAD framing with the transfer key

#### Scenario: Encrypted file upload

- **WHEN** a file upload is initiated by a client with active AEAD transport
- **THEN** the server SHALL read the HTXF handshake in plaintext, derive the transfer key from the reference number, and decrypt the incoming FILP stream using AEAD framing with the transfer key

#### Scenario: Encrypted banner download

- **WHEN** a banner download is initiated by a client with active AEAD transport
- **THEN** the server SHALL encrypt the banner data using AEAD framing with the per-transfer derived key

#### Scenario: Non-AEAD connection file transfer

- **WHEN** a file transfer is initiated by a client using RC4 or no HOPE transport
- **THEN** file transfers SHALL remain unencrypted (existing behavior unchanged)

### Requirement: Encrypted login reply timing

The authenticated login (Step 3) SHALL be received in plaintext. AEAD transport keys MUST be activated immediately after validating the login but before sending the login reply. The server encrypts its login reply as the first AEAD frame.

#### Scenario: Login reply sent through AEAD

- **WHEN** AEAD transport is negotiated and the authenticated login has been validated
- **THEN** the server SHALL activate AEAD encryption, and the login reply SHALL be the first AEAD-sealed frame sent to the client (nonce counter = 0)

### Requirement: Cipher policy configuration

The server SHALL support a `hope_cipher_policy` configuration field that controls cipher negotiation behavior.

Valid values:
- `prefer-aead` (default): select CHACHA20-POLY1305 if the client supports it and MAC is compatible; otherwise fall back to RC4
- `require-aead`: only accept CHACHA20-POLY1305; reject HOPE negotiation if the client does not support it or MAC is INVERSE
- `rc4-only`: only offer RC4 (existing behavior, ignores CHACHA20-POLY1305 in client's list)

#### Scenario: Default policy

- **WHEN** `hope_cipher_policy` is not set in the config
- **THEN** the server SHALL default to `prefer-aead`

#### Scenario: require-aead rejects RC4-only client

- **WHEN** `hope_cipher_policy` is `require-aead` and a client's HOPE identification only includes RC4
- **THEN** the server SHALL reject the HOPE negotiation and close the connection

#### Scenario: rc4-only ignores AEAD

- **WHEN** `hope_cipher_policy` is `rc4-only` and a client offers CHACHA20-POLY1305
- **THEN** the server SHALL ignore it and only negotiate RC4

### Requirement: E2E AEAD gating

The server SHALL support an `e2e_require_aead` configuration field (boolean, default false). When enabled, the `hl_client_is_encrypted()` check SHALL return false for clients using RC4-only HOPE transport, requiring AEAD specifically for access to E2E-prefixed content.

#### Scenario: RC4 client denied E2E content when e2e_require_aead is true

- **WHEN** `e2e_require_aead` is true and a client has RC4 HOPE transport active (not AEAD)
- **THEN** `hl_client_is_encrypted()` SHALL return 0, and the client SHALL be denied access to E2E-prefixed files/folders/categories

#### Scenario: AEAD client allowed E2E content when e2e_require_aead is true

- **WHEN** `e2e_require_aead` is true and a client has AEAD HOPE transport active
- **THEN** `hl_client_is_encrypted()` SHALL return 1

#### Scenario: e2e_require_aead is false (default)

- **WHEN** `e2e_require_aead` is false (the default)
- **THEN** `hl_client_is_encrypted()` SHALL behave as before, returning 1 for any active HOPE transport (RC4 or AEAD) that is not INVERSE

### Requirement: GUI controls for AEAD configuration

The macOS AppKit GUI SHALL include controls for the new AEAD configuration fields in the existing "Security (HOPE)" settings section.

#### Scenario: Cipher policy popup

- **WHEN** the user opens the server settings
- **THEN** a popup button labeled "Cipher Policy" SHALL be present with three options: "Prefer AEAD (recommended)", "Require AEAD", and "RC4 Only", and SHALL persist to the `HOPECipherPolicy` plist key

#### Scenario: Require AEAD for E2E checkbox

- **WHEN** the user opens the server settings
- **THEN** a checkbox labeled "Require AEAD for E2E content" SHALL be present and SHALL persist to the `E2ERequireAEAD` plist key

#### Scenario: Updated HOPE help text

- **WHEN** the user views the help text for the "Enable HOPE Encryption" checkbox
- **THEN** the text SHALL mention ChaCha20-Poly1305 AEAD and encrypted file transfers as capabilities when the client supports them
