## Why

fogWraith's Hotline spec defines a WebRTC-based voice chat extension using a server-side SFU (Selective Forwarding Unit) architecture. Janus (fogWraith's Go server) has a working implementation, and Hotline Navigator supports it as a client. Lemoniscate has no voice support today.

This is Phase 1 of 3 — the signaling and plumbing layer. It adds the Hotline transactions (600-606), capability negotiation, access control, room voice state tracking, and the persistent UDP listener needed by the later phases. No audio flows yet, but the protocol surface is fully functional: clients can join/leave voice rooms, see participant lists, and toggle mute state.

Building this first lets us validate the transaction layer against Navigator without needing the full WebRTC media stack, and establishes the UDP socket infrastructure that Phases 2 and 3 build on.

**Reference**: [fogWraith Voice Chat spec](https://github.com/fogWraith/Hotline/blob/main/Docs/Protocol/Capabilities-Voice.md)
**Depends on**: Nothing (standalone)
**Followed by**: `voice-chat-webrtc` (Phase 2), `voice-chat-audio` (Phase 3)

## What Changes

### Capability negotiation

- Define `HL_CAPABILITY_VOICE` as `0x0004` (bit 2 in the `DATA_CAPABILITIES` bitmask, field 0x01F0).
- During login, if voice is enabled on the server, echo the `CAPABILITY_VOICE` bit in the capabilities response. Clients must see this bit to display voice UI.
- Voice is enabled when the server is compiled with `ENABLE_VOICE=1` and the config has `VoiceChat.Enabled: true`.

### Access control

- Define `ACCESS_VOICE_CHAT` as bit 55 in the access bitmap.
- The current 8-byte (64-bit) access bitmap has room — bits 41-63 are unused. Bit 55 fits within the existing wire format.
- Add `VoiceChat` to the GUI permission checkbox list and the YAML Access keys.
- Add `VoiceChat: true` to the admin account template. Guest template does not get it by default.
- Server rejects transaction 600 (join voice) if the user lacks this permission.
- The `CAPABILITY_VOICE` bit is echoed regardless of the user's permissions — clients should show the voice UI as disabled rather than hidden.

### Transactions 600-606

| ID | Name | Direction | Fields |
|----|------|-----------|--------|
| 600 | Join Room Voice | Client→Server | ChatID (0 = public lobby) |
| 601 | Leave Room Voice | Client→Server | ChatID |
| 602 | SDP Offer | Server→Client | ChatID, SDP string |
| 603 | SDP Answer | Client→Server | ChatID, SDP string |
| 604 | ICE Candidate | Bidirectional | ChatID, candidate string (empty = done) |
| 605 | Participant Status | Server→Client | ChatID, participant list (UserID + mute state) |
| 606 | Toggle Mute | Client→Server | ChatID, mute flag |

Transaction handlers follow the established pattern in `transaction_handlers_clean.c`. SDP and ICE fields are opaque strings at this phase — they are stored/forwarded but not parsed until Phase 2.

### Room voice state

Each chat room (public chat = ID 0, private rooms = ID > 0) gains voice session state:

```c
typedef struct {
    uint16_t user_id;
    int      muted;          /* server-enforced mute */
    /* Phase 2 adds: peer connection state, DTLS/SRTP state */
} hl_voice_participant_t;

typedef struct {
    int                      active;
    hl_voice_participant_t   participants[HL_VOICE_MAX_PARTICIPANTS];
    int                      participant_count;
} hl_voice_session_t;
```

- `HL_VOICE_MAX_PARTICIPANTS` defaults to 16 (configurable).
- A user may only be in voice in one room at a time. Joining a second room implicitly leaves the first.
- When a user disconnects from the server, they are removed from any voice session.
- When a private chat room is closed, its voice session ends.
- Participant status (transaction 605) is broadcast to all voice participants when someone joins, leaves, or toggles mute.

### UDP listener

- Open a persistent UDP socket on `server_port + 4` (per the spec, e.g., 5504 if the server runs on 5500).
- Register with the kqueue event loop via `EVFILT_READ`.
- At this phase, the UDP socket exists and is listening but does not process any media — it only needs to be ready for Phase 2's STUN/DTLS/RTP traffic.
- The socket is only opened if voice is enabled.

### Configuration

Add to `config.yaml`:

```yaml
VoiceChat:
  Enabled: false
  MaxParticipantsPerRoom: 16
```

Add to the GUI plist:
- `VoiceChatEnabled` (bool, default false)
- `VoiceChatMaxParticipants` (int, default 16)

Add a "Voice Chat" disclosure section to the GUI left panel:
- Enable checkbox
- Max participants per room field

### Compile flag

All voice code is gated behind `ENABLE_VOICE=1` in the Makefile. When not set:
- Voice source files are not compiled
- `HL_CAPABILITY_VOICE` is never echoed
- Transaction handlers 600-606 are not registered
- No UDP socket is opened
- The `hl_voice_session_t` fields are excluded from chat room structs via `#ifdef`

This keeps the default build unchanged — no new dependencies, no new attack surface.

## Capabilities

### New Capabilities
- `voice-chat-signaling`: Hotline voice chat transactions (600-606), capability advertisement, access control (bit 55), room voice session state, and UDP listener socket. Covers the signaling layer only — no WebRTC media.

### Modified Capabilities
- `user-management`: Access bitmap gains bit 55 (`VoiceChat`). GUI permission editor and YAML account keys updated.
- `chat-messaging`: Chat room structs gain optional voice session state.
- `server-config`: `config.yaml` gains `VoiceChat` section. GUI gains Voice Chat settings panel.
- `networking`: kqueue event loop gains a persistent UDP socket (voice media port).

## Impact

- **Server code**: `server.c` (UDP listener, voice enable check), `transaction_handlers_clean.c` (handlers for 600-606), `chat.c` / `chat.h` (voice session state on rooms), `types.h` (new transaction/field constants), `access.h` (bit 55), `config_loader.c` (VoiceChat config section).
- **GUI code**: `AppController.h` (voice settings properties), `AppController+LayoutAndTabs.inc` (Voice Chat settings section, VoiceChat permission checkbox), `AppController+LifecycleConfig.inc` (plist read/write), `AppController+AccountsData.inc` (permission template update).
- **Makefile**: `ENABLE_VOICE` flag, conditional source list.
- **Dependencies**: None new. This phase is pure C with no additional libraries.
- **Risk**: Low. Additive transactions, no changes to existing protocol flow. The UDP socket is inert at this phase.
