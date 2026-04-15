## Why

Lemoniscate's HOPE implementation only supports RC4 for transport encryption. RC4 is cryptographically broken (RFC 7465), provides no integrity guarantees, and cannot encrypt file transfers (HTXF runs on separate TCP connections outside the RC4 stream). Navigator v0.2.5 already ships with ChaCha20-Poly1305 AEAD support and encrypted file transfers, tested and working against VesperNet (Janus server). Lemoniscate needs the server-side counterpart so it can serve as a fully AEAD-capable HOPE server, enabling authenticated encryption with integrity for both control-plane transactions and file transfer data.

## What Changes

- Add `HMAC-SHA256` as a MAC algorithm (32-byte output natively fits ChaCha20's key size)
- Implement HKDF-SHA256 key expansion to derive 256-bit keys from shorter MAC outputs
- Add `CHACHA20-POLY1305` as a negotiable cipher alongside `RC4` in the HOPE handshake
- Implement AEAD framed transport: length-prefixed frames with 16-byte Poly1305 authentication tags for post-login control-plane transactions
- Implement deterministic 12-byte nonces with direction byte and monotonic counter
- Implement AEAD-encrypted HTXF file transfers with per-transfer keys derived from the HTXF reference number
- Add `hope_cipher_policy` config field (`prefer-aead`, `require-aead`, `rc4-only`) to control cipher negotiation behavior
- Add `e2e_require_aead` config field to gate E2E-prefixed content access on AEAD specifically (ensuring file transfers are also encrypted)
- Bundle a portable ChaCha20-Poly1305 C implementation for Tiger-through-modern macOS and Linux compatibility

## Capabilities

### New Capabilities
- `hope-aead`: ChaCha20-Poly1305 AEAD cipher negotiation, HKDF-SHA256 key derivation, framed AEAD transport for control connections, AEAD-encrypted HTXF file transfers, HMAC-SHA256 MAC support, and cipher policy configuration
- `chacha20-poly1305-portable`: Standalone portable C implementation of ChaCha20-Poly1305 AEAD and HKDF-SHA256, compatible with Tiger (10.4 PPC) through modern macOS and Linux without requiring OpenSSL or CommonCrypto ChaCha20 support

### Modified Capabilities
- `hope`: Update cipher negotiation to include CHACHA20-POLY1305 alongside RC4; add HMAC-SHA256 to MAC algorithm list; add CipherMode/Checksum fields to negotiation reply; update `hl_client_is_encrypted()` to distinguish AEAD from RC4 when `e2e_require_aead` is set
- `file-transfers`: When the control connection uses HOPE AEAD mode, HTXF file transfers use ChaCha20-Poly1305 encryption with per-transfer derived keys (previously always unencrypted under HOPE)
- `config`: New fields `hope_cipher_policy` and `e2e_require_aead` in YAML and plist config loaders

## Impact

- **Crypto dependencies**: New bundled ChaCha20-Poly1305 and HKDF-SHA256 C source files (no external library dependency added)
- **Protocol layer**: `hope.c` / `hope.h` gain HMAC-SHA256, HKDF, AEAD encrypt/decrypt; new `hope_aead.c` / `hope_aead.h` for AEAD framing
- **Connection logic**: `server.c` login flow branches on negotiated cipher mode (AEAD vs STREAM) to select transport; `process_client_data()` gains AEAD frame scanning alongside existing RC4 incremental decrypt
- **File transfers**: `handle_file_transfer_connection()` in `server.c` wraps the transfer socket in AEAD framing when the originating client's control connection uses AEAD mode
- **Config**: `config.h` gains `hope_cipher_policy` and `e2e_require_aead` fields; `config_loader.c` and `config_plist.c` parse them
- **E2E gating**: `transaction_handlers_clean.c` E2E checks updated to respect `e2e_require_aead`
- **Backward compatibility**: No breaking changes. RC4-only clients continue to work (unless `require-aead` policy is set). INVERSE MAC remains incompatible with AEAD.
