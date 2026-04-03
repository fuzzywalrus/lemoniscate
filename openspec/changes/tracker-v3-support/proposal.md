## Why

Lemoniscate currently speaks only v1 of the Hotline tracker protocol — a basic UDP registration with name, description, and user count. The v3 tracker protocol (specified in fogWraith/Hotline) adds TLV-encoded metadata extensions that let servers advertise capabilities (HOPE, TLS, hostname), content statistics (file count, news count, message board posts), and security features (HMAC-SHA256 signing, registration tokens). Implementing v3 makes Lemoniscate visible with richer metadata on modern trackers, and enables content index fields that integrate with Mnemosyne — trackers can surface file/news counts in server listings without clients needing to query Mnemosyne separately.

**Sequencing:** This change depends on `mnemosyne-support` being completed first, since the content index TLV fields (0x0450-0x0453) are most useful when the server already tracks and syncs that content. The HTTP client and content serialization from `mnemosyne-support` provide the content counts needed for the v3 registration fields.

## What Changes

- Extend the UDP registration datagram to append a v3 extension block ("H3" magic + TLV-encoded metadata fields) after the existing v1 Pascal strings.
- Advertise server capabilities: HOPE support (0x0301), TLS support (0x0302), TLS port (0x0303), server software name/version (0x0200), hostname (0x0101).
- Advertise content index statistics: news count (0x0450), message board post count (0x0451), file count (0x0452), total file size (0x0453). These counts come from the same content managers used by Mnemosyne sync.
- Support optional tags for server categorization (0x0310).
- Handle v3 registration acknowledgments from trackers: parse status codes, honor tracker-assigned heartbeat intervals, store registration tokens for anti-hijacking.
- Support HMAC-SHA256 signing of registration datagrams when a tracker requires it (0x0801).
- Fix the existing v1 quirk where TLS port is sent in the reserved field (should be 0x0000 in v1; TLS port belongs in the v3 TLV field 0x0303).
- Maintain full backward compatibility — v1 trackers ignore the trailing v3 extension block.

## Capabilities

### New Capabilities
- `tracker-v3-registration`: v3 tracker protocol extension block with TLV metadata, registration acknowledgment handling, HMAC signing, and registration token persistence. Covers the wire format, field encoding, ack parsing, and heartbeat interval compliance.

### Modified Capabilities
- `networking`: Tracker registration now includes v3 extension block appended to the UDP datagram. Registration ack handling requires reading UDP responses (currently fire-and-forget). Heartbeat interval may be tracker-dictated rather than fixed.
- `server-config`: New configuration keys for optional hostname advertisement, server tags, and per-tracker HMAC secrets.

## Impact

- **Affected code**: `tracker.c` (extend registration datagram), `tracker.h` (new TLV builder functions, ack parser), `server.c` (pass content counts to tracker registration, handle ack responses), `config_loader.c` (new config keys).
- **Wire format**: Registration datagrams grow by ~50-200 bytes depending on TLV fields. Fully backward compatible — v1 trackers stop parsing after Pascal strings.
- **Existing behavior**: v1 registration continues to work unchanged. The v3 block is appended, not replacing. The reserved/TLS-port field quirk should be fixed (send 0x0000 in v1 header, move TLS port to TLV 0x0303).
- **Dependencies**: Content counts (file count, news count, message board posts) come from the same managers used by `mnemosyne-support`. This change should be implemented after `mnemosyne-support`.
- **New state**: Registration token (from ack) must be persisted per-tracker and included in subsequent registrations. Tracker-assigned heartbeat interval overrides the configured default.

**Note:** This proposal captures the full scope based on the fogWraith/Hotline v3 tracker spec. Design, specs, and tasks will be generated when this change is ready to be worked on — after `mnemosyne-support` is complete. The spec documents may need revisiting as v3 tracker support is tested against live trackers.
