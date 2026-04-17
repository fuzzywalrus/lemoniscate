## Why

Hotline chat is stateless — messages exist only in flight. When a user connects, they see nothing before their arrival. Hotline Navigator compensates with client-side history storage, but that's per-device and per-client. A new device, a different client, or a fresh install starts with a blank slate.

Discord-style chat history is the single most requested modernization for Hotline. Server-side persistence with client-driven pagination lets users scroll back through conversation, catch up after being offline, and experience the server as a living community rather than a transient chatroom.

This extension is designed for adoption beyond Lemoniscate. The spec defines wire protocol and query semantics only — storage backend, encryption at rest, and retention policy are implementation-defined. A server implementer can use SQLite, JSONL flat files, an in-memory ring buffer, or anything else. The protocol follows established Hotline patterns (TLV fields, packed binary entries, capability bitmask negotiation) so anyone who can implement the existing protocol can implement this.

**Reference**: [Hotline Protocol Docs](https://github.com/fogWraith/Hotline/tree/main/Docs/Protocol)
**Protocol Specification**: [docs/Capabilities-Chat-History.md](../../docs/Capabilities-Chat-History.md)
**Implementation Design**: [design.md](./design.md) — storage, encryption, pruning, rate limiting, crash safety

## What Changes

### Capability negotiation

Chat History is advertised as **Bit 4** of `FIELD_CAPABILITIES` (0x01F0) during LOGIN.

- Client sets bit 4 to indicate history support.
- Server echoes bit 4 in the LOGIN reply if history is enabled.
- If the server echoes bit 4, the client MAY use `TRAN_GET_CHAT_HISTORY` (700) to request message batches.
- If the server does not echo bit 4, the client MUST NOT send history transactions.

The server MAY include retention policy hints in the LOGIN reply:
- `FIELD_HISTORY_MAX_MSGS` (0x0F07): uint32, maximum messages retained (0 = unlimited).
- `FIELD_HISTORY_MAX_DAYS` (0x0F08): uint32, maximum days retained (0 = unlimited).

These are informational — the client uses them for UI hints (e.g., "Server keeps 30 days of history") but does not depend on them for correctness.

### Transaction: TRAN_GET_CHAT_HISTORY (700)

Client requests a batch of historical messages. Request/reply model.

**Request fields:**

| Field | Type ID | Format | Required | Description |
|-------|---------|--------|----------|-------------|
| `FIELD_CHANNEL_ID` | 0x0F01 | uint32 | Yes | Channel to query. 0 = public chat. |
| `FIELD_HISTORY_BEFORE` | 0x0F02 | uint64 | No | Return messages with IDs less than this value. Absent = start from latest. |
| `FIELD_HISTORY_AFTER` | 0x0F03 | uint64 | No | Return messages with IDs greater than this value. |
| `FIELD_HISTORY_LIMIT` | 0x0F04 | uint16 | No | Maximum messages to return. Server default: 50. |

**Cursor rules:**
- Neither cursor present: return the most recent messages (up to limit).
- `BEFORE` only: return messages older than the cursor (scrolling back).
- `AFTER` only: return messages newer than the cursor (catching up).
- Both present: return messages in the range (after, before), up to limit.

**Reply fields:**

| Field | Type ID | Format | Required | Description |
|-------|---------|--------|----------|-------------|
| `FIELD_HISTORY_ENTRY` | 0x0F05 | binary | Repeated | One field per message, packed binary (see entry format). |
| `FIELD_HISTORY_HAS_MORE` | 0x0F06 | uint8 | Yes | 1 if more messages exist beyond this batch, 0 otherwise. |

Reply entries are ordered **oldest-first** (ascending message ID) regardless of cursor direction.

`FIELD_HISTORY_HAS_MORE` indicates whether additional messages exist **in the direction of the query**: older messages for `BEFORE`/no-cursor queries, newer messages for `AFTER` queries, more messages within the range for dual-cursor queries.

If the server has no messages matching the query, the reply contains zero `FIELD_HISTORY_ENTRY` fields and `FIELD_HISTORY_HAS_MORE` = 0.

**Limit clamping:** Servers MAY enforce a maximum limit (e.g., 200) regardless of the client's requested value. The server silently returns fewer entries — no error is raised.

### History entry format

Each `FIELD_HISTORY_ENTRY` contains a packed binary structure:

```
Fixed header (22 bytes):
  message_id   : uint64   (8)   Opaque cursor, monotonically increasing.
  timestamp    : int64    (8)   Unix epoch seconds.
  flags        : uint16   (2)   Bit 0: is_action (/me). Bit 1: is_server_msg.
                                Bit 2: is_deleted (tombstone). Bits 3-15: reserved (0).
  icon_id      : uint16   (2)   Sender's icon ID. 0 = none.
  nick_len     : uint16   (2)   Length of sender nickname in bytes.

Variable:
  nick         : bytes    (nick_len)
  msg_len      : uint16   (2)
  message      : bytes    (msg_len)

Optional sub-fields (mini-TLV, zero or more):
  sub_type     : uint16   (2)   Sub-field type.
  sub_len      : uint16   (2)   Sub-field data length.
  sub_data     : bytes    (sub_len)
```

Clients determine whether optional sub-fields are present by comparing bytes consumed against the field's total data length (known from the TLV field header). Clients MUST skip sub-fields with unrecognized types using `sub_len` to advance. No sub-field types are defined in v1 — the mechanism exists for future extension (e.g., colored nicknames, reply-to references, reactions).

Text encoding of `nick` and `message` follows the connection's negotiated encoding (Mac Roman or UTF-8). Servers transcode on retrieval, matching the behavior of live `TRAN_CHAT_MSG`.

**Tombstoned entries** (flag bit 2): `nick_len` and `msg_len` MAY be zero. The `message_id` and `timestamp` are preserved for cursor stability. Clients SHOULD display a placeholder (e.g., "[message removed]").

### Message IDs

Message IDs are **opaque uint64 values**. The only guarantee is monotonically increasing order — a higher ID means a newer message. Clients MUST NOT interpret, parse, or derive meaning from ID values. The generation scheme (sequential counter, timestamp-encoded, or otherwise) is implementation-defined.

### Channel semantics

`FIELD_CHANNEL_ID` defines a **persistent channel namespace**, separate from ephemeral private chats (`FIELD_CHAT_ID`):

- Channel 0: Public chat (the existing global chatroom). Servers MUST support this.
- Channel 1+: Named persistent channels. RESERVED for future specification.

Servers MUST NOT store history for ephemeral private chats (those using `FIELD_CHAT_ID`). Chat history applies only to persistent channels. Clients MAY store private chat history locally at the user's discretion.

### Access control

A new access privilege bit is defined:

- **Bit 56**: `ACCESS_READ_CHAT_HISTORY` — permission to request chat history.

Servers SHOULD check this bit when processing `TRAN_GET_CHAT_HISTORY`. If the bit is not configured in the server's access system, servers SHOULD fall back to checking `ACCESS_READ_CHAT` (bit 9). This allows simple implementations to reuse the existing chat permission while giving administrators granular control when needed.

### Legacy broadcast

Servers MAY send recent chat messages as standard `TRAN_CHAT_MSG` (106) transactions to clients that do not advertise the Chat History capability (bit 4). The timing, quantity, and formatting of these messages is implementation-defined.

### Server configuration

The extension is entirely optional for the server. A server that does not enable chat history simply does not echo bit 4 in the LOGIN reply. Recommended configuration surface (implementation-defined):

- Enable/disable chat history.
- Maximum messages retained (0 = unlimited).
- Maximum retention days (0 = unlimited).
- Legacy broadcast: enable/disable and message count.
- Encryption at rest: key or key file path.

### Storage

The spec defines wire protocol and query semantics only. Storage backend, file format, indexing strategy, and encryption at rest are implementation-defined. Implementations are encouraged to support encryption at rest but the mechanism is not specified.

## Capabilities

### New Capabilities
- `chat-history`: Server-side chat message persistence with cursor-based pagination, capability negotiation via bit 4, bidirectional batch retrieval, extensible entry format with mini-TLV sub-fields, and legacy broadcast fallback for unsupported clients.

### Modified Capabilities
- `chat-messaging`: Public chat messages are now optionally persisted (when chat history is enabled). The live broadcast path (`TRAN_CHAT_MSG` 106) is unchanged.

## New Protocol Constants

### Transaction Types
| Name | Value | Description |
|------|-------|-------------|
| `TRAN_GET_CHAT_HISTORY` | 700 (0x02BC) | Request a batch of chat history |
| 701-702 | Reserved | Future channel management |
| 703 | Reserved | Future message deletion |
| 704 | Reserved | Future message editing |
| 705-709 | Reserved | Future chat history extensions |

### Field Types
| Name | Value | Format | Description |
|------|-------|--------|-------------|
| `FIELD_CHANNEL_ID` | 0x0F01 (3841) | uint32 | Persistent channel identifier |
| `FIELD_HISTORY_BEFORE` | 0x0F02 (3842) | uint64 | Cursor: messages before this ID |
| `FIELD_HISTORY_AFTER` | 0x0F03 (3843) | uint64 | Cursor: messages after this ID |
| `FIELD_HISTORY_LIMIT` | 0x0F04 (3844) | uint16 | Maximum messages in response |
| `FIELD_HISTORY_ENTRY` | 0x0F05 (3845) | binary | Packed history entry |
| `FIELD_HISTORY_HAS_MORE` | 0x0F06 (3846) | uint8 | 1 = more messages available |
| `FIELD_HISTORY_MAX_MSGS` | 0x0F07 (3847) | uint32 | Server retention: max messages |
| `FIELD_HISTORY_MAX_DAYS` | 0x0F08 (3848) | uint32 | Server retention: max days |
| 0x0F09-0x0F1F | Reserved | — | Future channel management fields |

### Access Privilege Bits
| Name | Bit | Description |
|------|-----|-------------|
| `ACCESS_READ_CHAT_HISTORY` | 56 | Permission to request chat history |

## Impact

- **Server code**: New transaction handler for 700. Chat message recording hook in the existing `handle_chat_send` path. Storage backend (implementation choice). New fields in login reply when history is enabled. Access check for bit 56 with fallback to bit 9.
- **Protocol constants**: New transaction type, 8 new field types, 1 new access bit. All in previously unused ranges.
- **Configuration**: New `ChatHistory` section in config.yaml with enable, retention, legacy broadcast, and encryption settings.
- **Dependencies**: Depends on storage backend choice. Spec is deliberately agnostic.
- **Rate limiting**: Servers SHOULD enforce rate limits on history requests (recommended: 10 requests/second/client) to prevent abuse. Implementation-defined.
- **Risk**: Low-medium. The protocol extension is straightforward and follows established patterns. Primary risk is storage performance at scale (large histories with frequent pagination) — but that's an implementation concern, not a protocol concern.
- **Backward compatibility**: Full. Clients that don't advertise bit 4 see no change. Servers that don't enable history see no change. The legacy broadcast path uses existing transaction types.
