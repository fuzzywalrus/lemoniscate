## Context

Lemoniscate is a C Hotline server targeting Tiger (10.4 PPC) through modern macOS and Linux. It uses a kqueue/epoll event loop with non-blocking I/O and a 64KB per-client read buffer. The existing HOPE implementation uses an incremental RC4 decryption state machine (`hl_hope_decrypt_incremental`) that processes partial reads byte-by-byte through the RC4 keystream -- this works because RC4 is a stream cipher where each byte can be decrypted independently.

ChaCha20-Poly1305 is fundamentally different: it's an AEAD cipher operating on sealed frames. A frame cannot be decrypted until all its bytes (ciphertext + 16-byte Poly1305 tag) are available. This means the incremental byte-at-a-time approach won't work for AEAD -- we need frame-level buffering before decryption.

Navigator (the Rust/Tauri client) already has a complete, tested AEAD implementation (v0.2.5) that we must be wire-compatible with. The key derivation chain, nonce construction, frame format, and file transfer key derivation are all defined by what Navigator and the Janus (VesperNet) server already implement.

Platform crypto constraints: CommonCrypto on older macOS (Tiger through Snow Leopard) does not expose ChaCha20-Poly1305. OpenSSL 1.1+ on Linux does via EVP, but we can't depend on it being available everywhere. We need a portable C implementation.

## Goals / Non-Goals

**Goals:**
- Wire-compatible AEAD with Navigator v0.2.5 and Janus/VesperNet
- Negotiate ChaCha20-Poly1305 when the client supports it, with graceful RC4 fallback
- Encrypt HTXF file transfers when AEAD mode is active on the control connection
- Configurable cipher policy (prefer-aead, require-aead, rc4-only)
- E2E content gating that can distinguish AEAD from RC4 encryption
- Zero changes to existing RC4 behavior -- both paths coexist
- Portable across Tiger PPC through modern Apple Silicon and Linux x86/ARM

**Non-Goals:**
- Client-side changes (Navigator already has this)
- AEAD for folder transfers (complex multi-action protocol, separate proposal exists)
- Compression (`HopeServerCompression`/`HopeClientCompression` are orthogonal)
- Replacing or removing RC4 -- legacy clients need it
- AEAD upload verification (separate proposal exists)

## Decisions

### Decision 1: Bundled portable ChaCha20-Poly1305 implementation

Bundle standalone C source for ChaCha20-Poly1305 (RFC 8439) rather than depending on OpenSSL EVP or platform APIs.

**Rationale:** CommonCrypto on Tiger/Leopard/Snow Leopard has no ChaCha20. OpenSSL is only available on Linux builds and the version varies. A ~500-line portable C implementation (ChaCha20 quarter-round + Poly1305 multiply-accumulate) has zero dependencies and compiles on any C89 target. This matches Lemoniscate's existing approach of manual RC4 and HMAC implementations for Tiger compatibility.

**Alternative considered:** Platform abstraction (CommonCrypto on modern macOS, OpenSSL on Linux, bundled fallback on old macOS). Rejected because the abstraction complexity outweighs the benefit -- ChaCha20-Poly1305 is simple enough that one portable implementation is cleaner than three platform-specific ones.

**Source:** Adapt from a well-tested public-domain or permissive-licensed reference implementation (e.g., RFC 8439 reference code, or D.J. Bernstein's ref10 implementation).

### Decision 2: AEAD frame scanning in the kqueue read buffer

Add a new AEAD frame scanner that operates on the existing 64KB `read_buf` alongside the RC4 incremental decrypt path. The AEAD scanner checks whether a complete frame (4-byte length prefix + ciphertext + tag) has been buffered, and only then decrypts it in-place and hands it to the transaction parser.

**Rationale:** The kqueue event loop delivers partial reads. RC4 handles this with a 3-phase state machine that can decrypt byte-by-byte. AEAD can't do that -- it needs the full sealed frame. But we don't need a fundamentally different architecture: just check `if (read_buf_len >= 4 + frame_len)` before attempting decryption. The 64KB read buffer is large enough for typical Hotline transactions (most are under 1KB, the maximum is capped at 16 MiB but we reject frames over 64KB in practice to fit the buffer).

**Frame size vs buffer size:** The AEAD spec allows 16 MiB frames. In practice, Hotline transactions are small. If a frame exceeds `sizeof(read_buf)`, we must either reject it or dynamically allocate. For simplicity, reject frames larger than 64KB with a protocol error -- this matches the existing `MAX_TRANSACTION_BODY_SIZE` constraint. If a legitimate use case arises for larger frames, we can add dynamic allocation later.

**Alternative considered:** Separate AEAD module with its own buffering (like Navigator's `HopeAeadReader`). Rejected because Navigator uses async I/O where the reader owns the stream -- Lemoniscate's event loop owns the buffer and dispatches to handlers, so the framing logic needs to live in the event loop's read path.

### Decision 3: Separate AEAD state struct alongside existing hope_state

Add `hl_hope_aead_state_t` as a member of the existing `hl_hope_state_t` rather than creating a parallel state struct. This keeps all HOPE state in one place per client.

```c
typedef struct {
    uint8_t  encode_key[32];   /* server -> client */
    uint8_t  decode_key[32];   /* client -> server */
    uint64_t send_counter;     /* nonce counter for outbound */
    uint64_t recv_counter;     /* nonce counter for inbound */
    uint8_t  ft_base_key[32];  /* file transfer base key */
    int      ft_base_key_set;  /* 1 if ft_base_key is valid */
} hl_hope_aead_state_t;
```

The existing `hl_hope_state_t` gains:
- `int aead_active;` flag (separate from `active` which covers RC4)
- `hl_hope_aead_state_t aead;` inline struct

**Rationale:** The AEAD state is small (fixed-size, no heap allocation). Embedding it avoids a separate malloc and keeps the cleanup path simple. The `aead_active` flag lets all transport code branch with a single check: `if (hope->aead_active)` for AEAD, `else if (hope->active)` for RC4.

### Decision 4: Cipher negotiation policy via config enum

Implement `hope_cipher_policy` as a string config field mapping to an internal enum:

```c
typedef enum {
    HL_HOPE_CIPHER_PREFER_AEAD,  /* default: AEAD if client supports, else RC4 */
    HL_HOPE_CIPHER_REQUIRE_AEAD, /* reject RC4-only HOPE clients */
    HL_HOPE_CIPHER_RC4_ONLY      /* existing behavior, for testing */
} hl_hope_cipher_policy_t;
```

**Negotiation logic in `hl_hope_build_negotiation_reply()`:**
1. Parse client's cipher list for both CHACHA20-POLY1305 and RC4 support
2. Check selected MAC algorithm -- INVERSE is incompatible with AEAD
3. Apply policy:
   - `prefer-aead`: if client supports AEAD and MAC is not INVERSE, select AEAD; else fall back to RC4
   - `require-aead`: if client supports AEAD and MAC is not INVERSE, select AEAD; else return -1 (reject)
   - `rc4-only`: only offer RC4 (ignore AEAD in client's list)

**Rationale:** Server operators need control. An operator running a server with sensitive E2E content might want `require-aead` to guarantee file transfers are encrypted. An operator running a general-purpose server wants `prefer-aead` for best available security with backward compatibility. `rc4-only` is the escape hatch for testing or if AEAD causes issues.

### Decision 5: AEAD fields in negotiation reply

When AEAD is selected, the server's negotiation reply includes:

| Field | Value |
|-------|-------|
| `FIELD_HOPE_SERVER_CIPHER` | `"CHACHA20-POLY1305"` (encoded as cipher selection) |
| `FIELD_HOPE_CLIENT_CIPHER` | `"CHACHA20-POLY1305"` (encoded as cipher selection) |
| `FIELD_HOPE_SERVER_CIPHER_MODE` | `"AEAD"` (4 raw ASCII bytes) |
| `FIELD_HOPE_CLIENT_CIPHER_MODE` | `"AEAD"` (4 raw ASCII bytes) |
| `FIELD_HOPE_SERVER_CHECKSUM` | `"AEAD"` (4 raw ASCII bytes) |
| `FIELD_HOPE_CLIENT_CHECKSUM` | `"AEAD"` (4 raw ASCII bytes) |

These are in addition to the existing session key, MAC algorithm, and login fields. The CipherMode and Checksum fields are what Navigator uses to detect AEAD mode.

### Decision 6: File transfer AEAD key derivation

Reuse Navigator's key derivation chain exactly:

```
ft_base_key = HKDF-SHA256(
    ikm  = encode_key_256 || decode_key_256,  (64 bytes)
    salt = session_key,                        (64 bytes)
    info = "hope-file-transfer"
)

transfer_key = HKDF-SHA256(
    ikm  = ft_base_key,
    salt = ref_number_bytes,    (4 bytes, big-endian)
    info = "hope-ft-ref"
)
```

Store `ft_base_key` in the AEAD state after login. Look up the originating client for each file transfer connection by matching the HTXF reference number to the transfer entry, which links back to the client. Derive the per-transfer key and wrap the transfer socket's I/O in AEAD framing.

**Rationale:** Must be wire-compatible with Navigator. The derivation chain is deterministic -- both sides derive the same key from shared state.

### Decision 7: E2E gating with AEAD awareness

Add `e2e_require_aead` (bool, default false) config field. When set:
- `hl_client_is_encrypted()` returns false for RC4-only HOPE clients
- Only AEAD clients can access E2E-prefixed content
- This is stronger than the existing check because it guarantees file transfers are also encrypted (RC4 HOPE leaves file transfers unencrypted)

The existing `e2e_require_tls` continues to work independently -- it checks TLS on the control connection. An operator might set both `e2e_require_aead` and `e2e_require_tls` for maximum security (AEAD-encrypted transactions + TLS-encrypted file transfer channel).

### Decision 8: HMAC-SHA256 and HKDF via platform crypto

HMAC-SHA256 follows the same manual HMAC construction already used for HMAC-SHA1 and HMAC-MD5 in `hope.c`, using the platform's SHA-256 primitive:
- macOS: `CC_SHA256` (available since Tiger)
- Linux: OpenSSL `SHA256`

HKDF-SHA256 (RFC 5869) is implemented as two function calls:
- `hl_hkdf_sha256_extract(salt, salt_len, ikm, ikm_len, prk_out)` -- HMAC-SHA256(salt, ikm)
- `hl_hkdf_sha256_expand(prk, info, info_len, okm, okm_len)` -- standard expand loop

Convenience wrapper: `hl_hkdf_sha256(ikm, ikm_len, salt, salt_len, info, info_len, out_32)`.

**Rationale:** SHA-256 is available on all target platforms. The manual HMAC construction is already proven in the codebase for SHA-1 and MD5.

## Risks / Trade-offs

**[Risk] 64KB read buffer may be too small for some AEAD frames** -> Reject frames larger than 64KB. In practice, Hotline transactions are small (chat messages, file listings, user lists). The only potentially large transaction is news article content, which is still well under 64KB. If this becomes an issue, dynamic allocation can be added later. Mitigation: log a clear error when rejecting oversized frames so operators can report the issue.

**[Risk] Bundled ChaCha20-Poly1305 implementation correctness** -> Use a well-tested reference implementation and validate against known test vectors from RFC 8439. Also validate interop against Navigator + VesperNet (both already have working implementations). The encrypt/decrypt round-trip tests are deterministic and reproducible.

**[Risk] INVERSE MAC + AEAD edge case** -> If a server is set to `require-aead` but a client only supports INVERSE MAC, the negotiation must fail gracefully. INVERSE cannot produce cryptographic key material for HKDF. Mitigation: detect this combination and reject during negotiation, sending an error reply before closing the connection.

**[Risk] File transfer client lookup** -> The HTXF protocol doesn't carry the client ID in the transfer header -- only the reference number. We need to look up which client initiated the transfer to determine if AEAD is active. Mitigation: store the client ID (or a pointer to the AEAD state / ft_base_key) in the `hl_file_transfer_t` entry when the transfer is created by the transaction handler. This is a small addition to the existing transfer struct.

**[Trade-off] No key rotation in AEAD mode** -> Unlike RC4 which rotates keys per-packet, ChaCha20-Poly1305 relies on unique nonces. This is standard AEAD practice and is safe as long as nonces are never reused, which the monotonic counter guarantees. The counter is 64-bit, so overflow is not a concern for any practical connection lifetime.

**[Trade-off] `require-aead` rejects legacy clients** -> Some older Hotline clients only support RC4 HOPE or no HOPE at all. Setting `require-aead` prevents these clients from connecting with HOPE (they can still connect without HOPE if `enable_hope` is true but HOPE is opt-in on the client). This is intentional -- operators who set `require-aead` are explicitly choosing security over compatibility.

**[Trade-off] Increased binary size from bundled crypto** -> The ChaCha20-Poly1305 implementation adds ~500-800 lines of C. This is modest and consistent with the existing approach of bundling RC4 and HMAC implementations.

### Decision 9: GUI updates for new config fields

The macOS AppKit GUI already has a "Security (HOPE)" section in `AppController+LayoutAndTabs.inc` with controls for EnableHOPE, Legacy Mode, E2E Prefix, and Require TLS. Two new controls are needed:

1. **Cipher Policy popup** (`_hopeCipherPolicyPopup`): An `NSPopUpButton` with three items: "Prefer AEAD (recommended)", "Require AEAD", "RC4 Only". Placed after the Legacy Mode checkbox. The popup maps to the `HOPECipherPolicy` plist key with string values `"prefer-aead"`, `"require-aead"`, `"rc4-only"`.

2. **Require AEAD for E2E checkbox** (`_hopeRequireAEADCheckbox`): An `NSButton` switch titled "Require AEAD for E2E content". Placed after the existing "Require TLS for E2E file transfers" checkbox. Help text: "When enabled, only clients using ChaCha20-Poly1305 AEAD encryption can access E2E-prefixed content. This guarantees file transfers are also encrypted (RC4 HOPE does not encrypt file transfers)." Maps to the `E2ERequireAEAD` plist key.

Both controls follow the existing pattern: declared as ivars in the AppController, created in `LayoutAndTabs.inc`, read/written in `LifecycleConfig.inc`, and saved to the plist config.

The help text for the existing "Enable HOPE Encryption" checkbox should be updated to mention AEAD: "HOPE replaces plaintext password login with a challenge-response MAC handshake. When the client supports it, the connection is upgraded to ChaCha20-Poly1305 AEAD encryption (including file transfers). Legacy clients fall back to RC4 stream encryption for the command channel only."
