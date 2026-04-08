## Why

Phases 1 and 2 established the signaling layer (transactions 600-606) and WebRTC handshake stack (STUN, DTLS, SRTP). At the end of Phase 2, a WebRTC peer connection is established between Navigator and Lemoniscate — but no audio flows. This phase completes the picture: receive RTP audio packets from each participant, and forward (fan out) to all other participants in the same voice room. This is the SFU (Selective Forwarding Unit) core.

**Reference**: [fogWraith Voice Chat spec](https://github.com/fogWraith/Hotline/blob/main/Docs/Protocol/Capabilities-Voice.md)
**Depends on**: `voice-chat-webrtc` (Phase 2)

## What Changes

### RTP packet handling

Incoming UDP datagrams classified as RTP (first byte 0x80-0xBF) are processed:

1. **SRTP unprotect** — decrypt and verify authentication tag using the participant's recv SRTP context (from Phase 2).
2. **RTP header parse** — extract SSRC, sequence number, timestamp, payload type. Verify payload type is 0 (PCMU). Drop anything else.
3. **Mute check** — if the sender is muted (server-enforced via transaction 606), discard the packet. No further processing.
4. **Fan out** — for each other participant in the same voice room:
   - Rewrite the SSRC to match the receiver's expected SSRC for this sender's track (mapped via the `a=mid:user-{UID}` in the SDP).
   - SRTP protect the packet using the receiver's send SRTP context.
   - `sendto()` the encrypted packet to the receiver's UDP address.

```
Sender A                 Server (SFU)              Receiver B
   │                         │                         │
   │──RTP (SRTP)────────────▶│                         │
   │                         │  unprotect              │
   │                         │  check mute             │
   │                         │  rewrite SSRC           │
   │                         │  protect (B's key)      │
   │                         │──RTP (SRTP)────────────▶│
   │                         │                         │
   │                         │  (same for C, D, ...)   │
```

### SSRC management

Each participant's SDP offer assigns expected SSRCs for receive tracks. The server maintains a mapping:

```c
typedef struct {
    uint16_t  sender_uid;      /* Hotline user ID of the audio source */
    uint32_t  sender_ssrc;     /* SSRC the sender actually uses */
    uint32_t  receiver_ssrc;   /* SSRC the receiver expects for this sender */
} hl_ssrc_mapping_t;
```

When forwarding, the server replaces the SSRC in the RTP header. Sequence numbers and timestamps are passed through unmodified — the SFU does not transcode or repacketize.

### RTCP Sender Reports

The server sends periodic RTCP Sender Reports (SR) to each participant for each receive track. This tells the client's jitter buffer that the stream is alive and provides synchronization info.

Minimal SR contents:
- SSRC matching the receive track
- NTP timestamp (current wall clock)
- RTP timestamp (derived from packet count × 160 samples per PCMU frame)
- Sender packet count and octet count

Frequency: one SR per stream every 5 seconds (standard RTCP interval for low-bandwidth audio).

RTCP Receiver Reports from clients can be parsed minimally (extract round-trip time if useful for diagnostics) or ignored entirely — the SFU doesn't adapt based on receiver feedback since PCMU is fixed-rate.

### Mute enforcement

When a user sends transaction 606 (Toggle Mute):
- Server updates `participant.muted` flag.
- Server broadcasts transaction 605 (Participant Status) with updated mute states.
- **Server-enforced**: muted participants' RTP packets are discarded at the unprotect stage, before fanout. The server does not rely on the client to stop sending audio.

### Participant join/leave — SDP renegotiation

When a new participant joins an active voice session:
1. Existing participants receive a new SDP offer (transaction 602) with an additional `recvonly` media section for the new user's track.
2. Existing participants send back SDP answers (transaction 603).
3. The new participant's SDP offer already includes `recvonly` sections for all existing participants.

When a participant leaves:
1. Existing participants receive a new SDP offer with the departed user's media section removed.
2. The server stops forwarding the departed user's audio.

### Bandwidth considerations

Per the spec, G.711 μ-law at 64 kbps per stream:

| Participants | Server inbound | Server outbound (fanout) | Total |
|-------------|----------------|--------------------------|-------|
| 2 | 128 kbps | 128 kbps | 256 kbps |
| 5 | 320 kbps | 1.28 Mbps | 1.6 Mbps |
| 10 | 640 kbps | 5.76 Mbps | 6.4 Mbps |
| 16 | 1.02 Mbps | 15.36 Mbps | 16.4 Mbps |

No silence suppression — bandwidth is constant regardless of whether anyone is talking. This is a property of PCMU (no Voice Activity Detection in the spec).

The `MaxParticipantsPerRoom` config (from Phase 1) caps the fanout cost.

### Packet processing budget

At 8000 Hz / 160 samples per frame, PCMU produces 50 packets per second per participant. With 16 participants, the server processes 800 inbound packets/sec and sends 12,000 outbound packets/sec (each inbound forwarded to 15 others).

The kqueue UDP read handler should batch-read multiple datagrams per event (`recvmmsg` on Linux, loop of `recvfrom` on macOS) to avoid per-syscall overhead at higher participant counts.

### Session teardown

A voice session ends when:
- All participants leave (explicit or disconnect)
- A private chat room is closed
- The server shuts down (graceful: send RTCP BYE, close DTLS)

On teardown:
- Send RTCP BYE to all participants
- Close DTLS sessions (`SSL_shutdown`)
- Free SRTP contexts
- Clean up participant state

### Error handling

- **Packet too short**: Drop silently (could be a fragment or probe).
- **SRTP auth failure**: Drop the packet. Log at debug level (could be a late packet with rotated keys, or corruption).
- **Unknown SSRC**: Drop. Could be a client sending before the SDP is finalized.
- **Participant timeout**: If no RTP/RTCP received from a participant for 30 seconds, treat as disconnected — remove from session, broadcast 605 update.

## Capabilities

### New Capabilities
- `voice-chat-audio`: SFU audio forwarding — RTP receive, SRTP unprotect, SSRC remapping, fanout to room participants, SRTP protect, RTCP Sender Reports, mute enforcement via packet discard, and SDP renegotiation on participant join/leave.

### Modified Capabilities
- `voice-chat-signaling`: Mute (transaction 606) is now server-enforced via packet discard rather than being state-only.
- `voice-chat-webrtc`: SRTP protect/unprotect functions are now exercised for real audio traffic, not just key derivation.

## Impact

- **Server code**: New SFU forwarding logic (likely `voice_sfu.c`). Extends the kqueue UDP handler from Phase 2 with RTP classification and fanout. RTCP generation module. SDP renegotiation helpers.
- **Performance**: Packet processing is latency-sensitive — the forwarding path (unprotect → rewrite SSRC → protect → sendto) should minimize allocations and copies. Zero-copy where possible (modify SSRC in-place, protect into a pre-allocated output buffer).
- **Dependencies**: None beyond Phase 2 (OpenSSL for SRTP, already linked).
- **Risk**: Medium. The forwarding logic itself is straightforward (demux, rewrite, forward), but edge cases around participant join/leave during active audio, SDP renegotiation timing, and SRTP rollover (sequence number wrap at 2^48 packets — unlikely for audio but must not crash) need careful handling. Real-world testing with multiple Navigator clients is essential.
- **Testing**: Requires at least 3 Navigator clients connected simultaneously to verify the fanout path (2 clients only tests point-to-point, not the SFU N-1 fanout).
