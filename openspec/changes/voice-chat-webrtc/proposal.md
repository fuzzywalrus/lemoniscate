## Why

Phase 1 (`voice-chat-signaling`) established the Hotline transaction layer for voice chat — clients can join rooms, see participants, and exchange SDP/ICE strings. But the SDP and ICE data is opaque; no WebRTC peer connection is actually established. This phase builds the WebRTC handshake stack: STUN, DTLS, SRTP key derivation, and SDP offer generation. After this phase, a WebRTC peer connection successfully establishes between Navigator and Lemoniscate — but no audio is forwarded yet (that's Phase 3).

This is the go/no-go gate for the voice feature. If the DTLS handshake and SRTP keying work against Navigator, the remaining audio forwarding in Phase 3 is relatively straightforward. If interop issues surface here, they'll be caught before investing in the SFU fanout logic.

**Reference**: [fogWraith Voice Chat spec](https://github.com/fogWraith/Hotline/blob/main/Docs/Protocol/Capabilities-Voice.md)
**Depends on**: `voice-chat-signaling` (Phase 1)
**Followed by**: `voice-chat-audio` (Phase 3)
**Prerequisite investigation**: Obtain a pcap or SDP exchange sample from a working Janus ↔ Navigator voice session to validate SDP format expectations.

## What Changes

### STUN binding handler

When a UDP datagram arrives on the voice port, classify it by the first byte:

```
Byte 0-1 = 0x0001         → STUN Binding Request
Byte 0   = 0x14-0x17      → DTLS record
Byte 0   = 0x80-0xBF      → RTP/RTCP (Phase 3)
```

For STUN Binding Requests:
- Parse the 20-byte STUN header (type, length, magic cookie 0x2112A442, transaction ID).
- Reply with a Binding Response containing `XOR-MAPPED-ADDRESS` (the sender's IP:port XORed with the magic cookie).
- Validate `MESSAGE-INTEGRITY` using the ICE password from the SDP exchange.
- Include `MESSAGE-INTEGRITY` and `FINGERPRINT` in the response.

This is the ICE connectivity check from the server's perspective. Since the server has a known IP and offers only host candidates, there's no complex ICE state machine — just respond to binding requests.

### DTLS handshake (OpenSSL)

After ICE connectivity is established (successful STUN binding exchange), the client initiates a DTLS handshake over the same UDP socket.

Integration approach:
- Create an `SSL_CTX` with `DTLS_server_method()`.
- Use OpenSSL memory BIOs (`BIO_new(BIO_s_mem())`) to feed received UDP datagrams into OpenSSL and extract outbound DTLS records to send back.
- The DTLS handshake completes when `SSL_do_handshake()` returns 1.
- Generate a self-signed certificate per server instance (or per session). The certificate fingerprint goes into the SDP offer.

Per-participant DTLS state:

```c
typedef struct {
    SSL        *ssl;
    BIO        *rbio;         /* read BIO — feed received datagrams */
    BIO        *wbio;         /* write BIO — extract outbound datagrams */
    int         handshake_complete;
    uint8_t     srtp_key_material[60];  /* exported after handshake */
} hl_dtls_state_t;
```

After the DTLS handshake completes, export keying material for SRTP:

```c
SSL_export_keying_material(ssl, key_material, 60,
    "EXTRACTOR-dtls_srtp", 19, NULL, 0, 0);
```

This produces the master key and salt for both send and receive SRTP contexts.

### SRTP key derivation and context

From the DTLS-exported key material, derive SRTP keys per RFC 3711:

```
key_material layout (60 bytes for AES_128_CM_HMAC_SHA1_80):
  Client write key  (16 bytes)
  Server write key  (16 bytes)
  Client write salt (14 bytes)
  Server write salt (14 bytes)
```

Per-participant SRTP state:

```c
typedef struct {
    uint8_t   key[16];
    uint8_t   salt[14];
    /* AES key schedule for CTR mode */
    /* HMAC-SHA1 state for authentication */
} hl_srtp_context_t;

typedef struct {
    hl_srtp_context_t  send;    /* server → client */
    hl_srtp_context_t  recv;    /* client → server */
} hl_srtp_pair_t;
```

SRTP protect (encrypt + authenticate) and unprotect (verify + decrypt) functions operate on RTP packets. The mandatory cipher suite is `AES_128_CM_HMAC_SHA1_80`:
- AES-128 in Counter Mode for encryption
- HMAC-SHA1 truncated to 80 bits for authentication

OpenSSL provides both AES and HMAC-SHA1, so no additional crypto dependencies.

### SDP offer generation

When a client joins voice (transaction 600), the server builds an SDP offer:

```
v=0
o=- {session_id} 2 IN IP4 {server_ip}
s=-
t=0 0
a=group:BUNDLE {mid_list}
a=ice-ufrag:{random}
a=ice-pwd:{random}
a=fingerprint:sha-256 {dtls_cert_fingerprint}
a=setup:actpass

m=audio {port} UDP/TLS/RTP/SAVPF 0
c=IN IP4 {server_ip}
a=mid:send
a=sendonly
a=rtpmap:0 PCMU/8000
a=rtcp-mux

m=audio {port} UDP/TLS/RTP/SAVPF 0
c=IN IP4 {server_ip}
a=mid:user-{uid_1}
a=recvonly
a=rtpmap:0 PCMU/8000
a=rtcp-mux

...repeat for each existing participant...
```

Key points:
- One `sendonly` media section (client's microphone → server)
- One `recvonly` media section per other participant (server forwards their audio)
- `a=mid` labels map to user IDs: `user-{UID}` for receive tracks, `send` for the upload track
- All media is BUNDLE'd on a single port
- Only PCMU (payload type 0) — no codec negotiation needed
- `a=rtcp-mux` — RTCP on the same port as RTP

When a new participant joins, existing participants receive a re-offer with the additional receive track.

### SDP answer parsing

Parse the client's SDP answer to extract:
- `a=ice-ufrag` and `a=ice-pwd` — needed for STUN message integrity
- `a=setup:active` — confirms client will initiate DTLS
- `a=fingerprint` — for optional DTLS certificate verification

SDP parsing is narrow-scope: only extract the fields we need, ignore everything else. The server controls the offer format, so the answer structure is predictable.

### ICE candidate exchange

Server sends its host candidate(s) via transaction 604:

```
candidate:1 1 UDP 2130706431 {server_ip} {voice_port} typ host
```

Since the server has a known IP and no NAT, only host candidates are offered. The client may send server-reflexive or relay candidates, which the server can ignore (it only needs the direct path to work).

An empty candidate string signals end-of-candidates (trickle ICE completion).

### Participant ↔ peer connection mapping

The UDP socket receives datagrams from multiple clients on the same port. Demultiplexing by source IP:port maps each datagram to the correct participant's DTLS/SRTP state.

```c
/* Lookup table: remote addr → participant */
hl_voice_participant_t *hl_voice_find_by_addr(
    hl_voice_session_t *session,
    struct sockaddr_in *addr);
```

### Platform crypto extension

Extend `platform_crypto.h` with SHA-256 (needed for DTLS certificate fingerprint):

| Platform | SHA-256 |
|----------|---------|
| macOS | `CC_SHA256` (CommonCrypto) |
| Linux | `EVP_sha256()` (OpenSSL, already linked) |

DTLS and SRTP use OpenSSL directly (not through the platform abstraction) since they're gated behind `ENABLE_VOICE` and OpenSSL is the only viable DTLS implementation.

### macOS OpenSSL dependency (voice-only)

When `ENABLE_VOICE=1` on macOS:
- Link `-lssl -lcrypto` (from Homebrew or a bundled OpenSSL)
- The rest of the macOS build continues to use CommonCrypto and SecureTransport for non-voice crypto
- `ENABLE_VOICE` is off by default — no change to the standard macOS build

### Connection lifecycle

```
Client sends txn 600 (Join Voice)
    ↓
Server validates permission (ACCESS_VOICE_CHAT)
    ↓
Server adds participant to room voice session
    ↓
Server generates SDP offer → txn 602
    ↓
Client parses offer, sends SDP answer → txn 603
    ↓
Server/client exchange ICE candidates → txn 604
    ↓
Client sends STUN binding request to voice UDP port
    ↓
Server replies with STUN binding response
    ↓
Client initiates DTLS handshake over UDP
    ↓
Server completes DTLS via OpenSSL
    ↓
SRTP keys derived from DTLS export
    ↓
Peer connection established ✓
(Phase 3 enables actual audio flow)
```

### Timeouts

Per the spec:
- ICE connectivity check timeout: 10 seconds
- DTLS handshake timeout: 15 seconds
- Session establishment timeout: 30 seconds (join to peer connection ready)

On timeout, the participant is removed and a 605 (participant status) update is broadcast.

## Capabilities

### New Capabilities
- `voice-chat-webrtc`: WebRTC peer connection establishment for voice chat — STUN binding handler, DTLS handshake (OpenSSL), SRTP key derivation, SDP offer generation and answer parsing, ICE candidate exchange, and per-participant connection state management.

### Modified Capabilities
- `voice-chat-signaling`: Transactions 602 (SDP Offer), 603 (SDP Answer), and 604 (ICE Candidate) are now populated with real SDP/ICE data rather than being opaque placeholders.

## Impact

- **Server code**: New files for STUN, DTLS, SRTP, and SDP handling (likely `voice_stun.c`, `voice_dtls.c`, `voice_srtp.c`, `voice_sdp.c` under `src/hotline/`). Integration into the kqueue UDP read handler. Participant struct extended with DTLS/SRTP state.
- **Platform crypto**: `platform_crypto.h` gains SHA-256 API. Implementations in both `crypto_commoncrypto.c` and `crypto_openssl.c`.
- **Makefile**: `ENABLE_VOICE=1` adds `-lssl -lcrypto` on macOS. New source files conditionally included.
- **Dependencies**: OpenSSL on macOS (voice-only). Already linked on Linux.
- **Risk**: Medium-high. DTLS over UDP with memory BIOs is fiddly — retransmission timers, fragment reassembly, and handshake state must be handled correctly. Interop with Navigator's WebRTC stack is the primary risk. A pcap from a working Janus session is strongly recommended before implementation.
- **Tiger branch**: Not applicable — voice requires `ENABLE_VOICE=1` which is off by default and requires OpenSSL.
