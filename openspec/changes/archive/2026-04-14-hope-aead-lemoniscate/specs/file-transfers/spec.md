## MODIFIED Requirements

### Requirement: File transfers encryption behavior

File transfers (HTXF protocol on port+1) SHALL use separate TCP connections. The encryption behavior depends on the originating client's control connection transport mode:

- **No HOPE or INVERSE auth-only**: file transfers SHALL NOT be encrypted (existing behavior)
- **RC4 HOPE transport**: file transfers SHALL NOT be encrypted (existing behavior -- RC4 stream state is per-connection and cannot be shared to the transfer socket)
- **AEAD HOPE transport**: file transfers SHALL use ChaCha20-Poly1305 AEAD encryption with per-transfer derived keys

The HTXF handshake (4-byte "HTXF" magic + 4-byte reference number + remaining header) SHALL always be in plaintext regardless of encryption mode. AEAD framing begins immediately after the handshake is validated.

#### Scenario: File download on AEAD connection

- **WHEN** a file transfer is initiated by a client with active AEAD transport
- **THEN** the server SHALL derive the per-transfer key from the ft_base_key and HTXF reference number, and encrypt the FILP stream (headers + fork data) using AEAD framing after the plaintext HTXF handshake

#### Scenario: File upload on AEAD connection

- **WHEN** a file upload transfer connection arrives from a client with active AEAD transport
- **THEN** the server SHALL derive the per-transfer key and decrypt the incoming FILP stream using AEAD framing after the plaintext HTXF handshake

#### Scenario: Banner download on AEAD connection

- **WHEN** a banner download transfer connection arrives from a client with active AEAD transport
- **THEN** the server SHALL encrypt the banner data using AEAD framing with the per-transfer derived key

#### Scenario: File download on RC4 connection

- **WHEN** a file transfer is initiated by a client with active RC4 HOPE transport
- **THEN** the file data SHALL be sent unencrypted (existing behavior unchanged)

#### Scenario: Transfer entry stores AEAD state

- **WHEN** a transaction handler creates a file transfer entry for an AEAD-mode client
- **THEN** the transfer entry SHALL store sufficient state (ft_base_key or a reference to the client's HOPE state) to derive the per-transfer key when the HTXF connection arrives
