## Why

Lemoniscate's HOPE transport encryption currently uses RC4 (ARC4) — a stream cipher with known cryptographic weaknesses (biased keystream bytes, vulnerable to related-key attacks). The fogWraith HOPE spec defines a ChaCha20-Poly1305 AEAD extension that replaces RC4 with a modern authenticated cipher (RFC 8439), providing both confidentiality and integrity in a single pass. This is the cipher used by Hotline Navigator and Janus for encrypted sessions.

Without ChaCha20-Poly1305 support, Lemoniscate can only negotiate RC4 with modern clients. Clients that prefer or require AEAD ciphers will fall back to unencrypted transport.

**Reference**: [fogWraith HOPE ChaCha20-Poly1305 spec](https://github.com/fogWraith/Hotline/blob/main/Docs/Protocol/HOPE-ChaCha20-Poly1305.md)

## What Changes

### Cipher negotiation

Extend `hl_hope_build_negotiation_reply()` to recognize `CHACHA20-POLY1305` (and normalized variants) in the client's cipher list. When both sides support it, prefer ChaCha20-Poly1305 over RC4. The server selects the strongest mutually-supported cipher:

| Priority | Cipher | Mode |
|----------|--------|------|
| 1 (best) | CHACHA20-POLY1305 | AEAD |
| 2 | RC4 / RC4-128 / ARCFOUR | Stream |
| 3 | NONE | Plaintext |

When ChaCha20-Poly1305 is selected, the server sends:
- `FIELD_HOPE_SERVER_CIPHER` / `FIELD_HOPE_CLIENT_CIPHER`: `CHACHA20-POLY1305`
- `FIELD_HOPE_SERVER_CIPHER_MODE` / `FIELD_HOPE_CLIENT_CIPHER_MODE`: `AEAD`
- `FIELD_HOPE_SERVER_CHECKSUM` / `FIELD_HOPE_CLIENT_CHECKSUM`: `AEAD`

The mode and checksum fields set to `"AEAD"` signal that integrity is built into the cipher (no separate checksum needed). The INVERSE MAC algorithm is incompatible with AEAD mode — if INVERSE was negotiated for auth, cipher falls back to RC4 or NONE.

### Key derivation — HKDF-SHA256

RC4 uses the MAC-derived keys directly. ChaCha20-Poly1305 requires 256-bit keys derived via HKDF-SHA256:

```
For each direction (encode/decode):
  PRK = HKDF-Extract(salt=session_key, IKM=mac_derived_key)
  key = HKDF-Expand(PRK, info=direction_string, L=32)

Direction strings:
  Server -> Client: "hope-chacha-encode"
  Client -> Server: "hope-chacha-decode"
```

The `mac_derived_key` is the same encode_key/decode_key already produced by `hl_hope_derive_keys()`. HKDF expands these (typically 16-20 byte MAC outputs) to the 256-bit keys ChaCha20 requires.

### AEAD framing — replacing the stream cipher model

The current RC4 path operates as a byte stream: encrypt header bytes, then body bytes, using a continuous RC4 keystream with key rotation between transactions. ChaCha20-Poly1305 uses a fundamentally different model — framed AEAD:

```
RC4 (current):
┌──────────────────────────────────────────────┐
│ RC4_stream(header[20]) RC4_stream(body[...]) │
│ Continuous keystream, rotation after body    │
└──────────────────────────────────────────────┘

ChaCha20-Poly1305 (new):
┌─────────────────────────────────────────────────────┐
│ [length:4] [ciphertext + Poly1305 tag:N+16]         │
│ Each frame is independently sealed/opened            │
│ Nonce = direction(1) + zeros(3) + counter(8)        │
└─────────────────────────────────────────────────────┘
```

Key differences:
- **Frame-based, not streaming**: Each transaction is sealed as a complete unit with a 4-byte big-endian length prefix, followed by ciphertext and a 16-byte authentication tag.
- **No key rotation**: The nonce counter increments per frame instead. No `rotate_cipher_key()` calls.
- **Authentication built in**: Poly1305 tag provides integrity — tampering is detected. RC4 has no integrity protection.
- **Max frame size**: 16 MiB per the spec.

### Nonce construction

12-byte nonce per frame:

```
Byte 0:    Direction (0x00 = server->client, 0x01 = client->server)
Bytes 1-3: Zero padding
Bytes 4-11: Big-endian 64-bit counter (starts at 0, increments per frame)
```

Each direction maintains its own counter. Counter must never repeat for a given key.

### Per-connection cipher state changes

The `hl_hope_cipher_t` struct and `hl_hope_state_t` need to support both cipher types:

```
Current (RC4 only):
  hl_hope_cipher_t {
      hl_rc4_t  rc4;
      uint8_t   current_key[64];
      size_t    key_len;
  }

Extended (RC4 + ChaCha20):
  hl_hope_cipher_t {
      union {
          hl_rc4_t rc4;
          /* ChaCha20 state managed by platform crypto */
      };
      uint8_t   current_key[64];
      size_t    key_len;
      uint64_t  nonce_counter;   /* ChaCha20 frame counter */
  }

  hl_hope_state_t {
      ...existing fields...
      int cipher_type;  /* 0=RC4, 1=CHACHA20_POLY1305 */
  }
```

### Encrypt/decrypt path split

`hl_hope_encrypt_transaction()` and `hl_hope_decrypt_incremental()` must branch on cipher type:

- **RC4 path**: Unchanged — the existing three-phase state machine (HEADER -> BODY_PREFIX -> BODY_REST) with key rotation.
- **ChaCha20 path**: Seal the entire serialized transaction as one AEAD frame (length prefix + ciphertext + tag). On the decrypt side, read the 4-byte length, buffer until the full frame arrives, then open (decrypt + verify tag) as a single operation.

The incremental decrypt state machine for ChaCha20 is simpler than RC4's — only two phases: reading the length prefix, then reading the full ciphertext+tag frame. But it requires buffering the entire frame before decryption (AEAD can't decrypt partial frames).

### File transfer encryption

Per the spec, HTXF (file transfer) connections transmit the handshake in plaintext, then derive per-transfer encryption keys:

```
transfer_key = HKDF-Expand(
    PRK = HKDF-Extract(salt=ref_number_bytes, IKM=session_chacha_key),
    info = direction_string,
    L = 32
)
```

File data is then framed identically to control connection traffic (length + ciphertext + tag). This is a significant addition — the current HOPE implementation does not encrypt file transfers at all. File transfer encryption should be scoped as a follow-on or optional part of this change.

### Platform crypto extension

The `platform_crypto.h` abstraction currently provides only SHA-1 and MD5. ChaCha20-Poly1305 requires:

- **SHA-256** — for HKDF-SHA256 key derivation
- **HKDF** — Extract and Expand (can be built from HMAC-SHA256)
- **ChaCha20-Poly1305** — AEAD seal/open

Platform availability:
| Primitive | macOS (CommonCrypto) | Linux (OpenSSL) |
|-----------|---------------------|-----------------|
| SHA-256 | `CC_SHA256` | `EVP_sha256()` |
| HKDF | Manual (HMAC-SHA256 based) | `EVP_PKEY_derive` (3.0+) or manual |
| ChaCha20-Poly1305 | `CCCryptorCreateWithMode` (10.13+) | `EVP_chacha20_poly1305()` |

**Tiger compatibility note**: ChaCha20-Poly1305 is not available in CommonCrypto on Tiger (10.4). On the Tiger/PPC branch, this cipher would need to be either unavailable (fall back to RC4) or provided via a bundled implementation (e.g., libsodium's `crypto_aead_chacha20poly1305_ietf`, or a standalone C implementation). The proposal recommends graceful degradation: Tiger builds negotiate RC4, modern builds prefer ChaCha20.

### Configuration

No new configuration needed. Cipher negotiation is automatic — the server prefers ChaCha20-Poly1305 when the client supports it, falls back to RC4 otherwise. The existing HOPE enable/legacy mode settings continue to work as before.

An optional `PreferRC4: true` config key could force RC4 preference for debugging/interop, but this is low priority.

## Capabilities

### New Capabilities
- `hope-chacha20-poly1305`: ChaCha20-Poly1305 AEAD cipher for HOPE transport encryption — cipher negotiation, HKDF-SHA256 key derivation, framed AEAD encrypt/decrypt, nonce management, and platform crypto backends.

### Modified Capabilities
- `authentication`: HOPE cipher negotiation gains ChaCha20-Poly1305 as the preferred cipher. The login/MAC flow is unchanged — only the post-auth transport cipher changes.
- `wire-protocol`: Encrypted wire format gains an alternative framing model (length-prefixed AEAD frames vs. streaming RC4). Plaintext wire format is unchanged.

## Impact

- **Server code**: `hope.c` (cipher negotiation, key derivation, encrypt/decrypt path split), `hope.h` (extended cipher state structs, new cipher type enum), `server.c` (pass cipher type through connection lifecycle).
- **Platform crypto**: `platform_crypto.h` (add SHA-256, HKDF, ChaCha20-Poly1305 APIs), `crypto_commoncrypto.c` (macOS backend — requires 10.13+ for native ChaCha20, or bundled implementation), `crypto_openssl.c` (Linux backend — OpenSSL EVP).
- **File transfers**: `file_transfer.c` / `transfer.c` would need AEAD framing for encrypted transfers. This is a significant scope addition and could be deferred.
- **Makefile**: May need to link additional libraries if using a bundled ChaCha20 implementation for Tiger.
- **Wire compatibility**: Fully backward compatible. Clients that don't offer ChaCha20-Poly1305 get RC4 as before. The negotiation is additive.
- **Risk**: Medium. The encrypt/decrypt path split is the most complex part — the RC4 streaming model and ChaCha20 framing model are fundamentally different, and both must work correctly through the kqueue partial-read event loop. The AEAD path is actually simpler (no key rotation, no three-phase state machine) but requires full-frame buffering. Testing against Hotline Navigator is essential.
- **Tiger branch**: ChaCha20 is unavailable on Tiger's CommonCrypto. The Tiger build should compile without ChaCha20 support and negotiate RC4 only. This could be a compile-time `#ifdef` or a runtime capability check.

## Dependencies

- No external library dependencies if targeting macOS 10.13+ (CommonCrypto has ChaCha20) and Linux with OpenSSL 1.1+ (EVP has ChaCha20-Poly1305).
- Tiger/PPC builds would need either a bundled ChaCha20-Poly1305 implementation or graceful fallback to RC4-only.
- HKDF-SHA256 can be implemented manually from HMAC-SHA256 (RFC 5869) without additional dependencies.
