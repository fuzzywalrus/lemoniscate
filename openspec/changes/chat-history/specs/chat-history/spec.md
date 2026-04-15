## ADDED Requirements

### Requirement: Chat history capability negotiation
The server SHALL advertise chat history support via Bit 4 of FIELD_CAPABILITIES (0x01F0) in the LOGIN reply when history is enabled. Clients advertise support by setting Bit 4 in their LOGIN request.

#### Scenario: Client and server both support chat history
- **WHEN** a client sends a LOGIN with FIELD_CAPABILITIES bit 4 set
- **AND** the server has chat history enabled
- **THEN** the server echoes bit 4 in the LOGIN reply FIELD_CAPABILITIES
- **AND** the server MAY include FIELD_HISTORY_MAX_MSGS (0x0F07) and FIELD_HISTORY_MAX_DAYS (0x0F08) in the reply

#### Scenario: Client supports history but server does not
- **WHEN** a client sends a LOGIN with FIELD_CAPABILITIES bit 4 set
- **AND** the server does not have chat history enabled
- **THEN** the server does not echo bit 4 in the LOGIN reply
- **AND** the client does not send TRAN_GET_CHAT_HISTORY transactions

#### Scenario: Server supports history but client does not
- **WHEN** a client sends a LOGIN without FIELD_CAPABILITIES bit 4
- **AND** the server has chat history enabled
- **THEN** the server does not echo bit 4 in the LOGIN reply
- **AND** the server MAY send recent messages as TRAN_CHAT_MSG (106) per the legacy broadcast policy

### Requirement: Chat message persistence
The server SHALL record public chat messages to persistent storage when chat history is enabled. Each record includes at minimum: a server-assigned message ID, timestamp, sender nickname, and message text.

#### Scenario: Public chat message is recorded
- **WHEN** a client sends TRAN_CHAT_SEND (105) without FIELD_CHAT_ID (public chat)
- **AND** chat history is enabled
- **THEN** the server persists the message with a monotonically increasing uint64 message ID, Unix epoch timestamp, sender nickname, icon ID, and message text
- **AND** the server broadcasts the message to connected clients as normal via TRAN_CHAT_MSG (106)

#### Scenario: Private chat message is not recorded
- **WHEN** a client sends TRAN_CHAT_SEND (105) with FIELD_CHAT_ID (private/ephemeral chat)
- **THEN** the server MUST NOT persist the message to chat history storage
- **AND** the server delivers the message to chat members as normal

#### Scenario: Server message or admin broadcast is recorded
- **WHEN** a server message or admin broadcast is sent
- **AND** chat history is enabled
- **THEN** the server MAY persist the message with the is_server_msg flag (bit 1) set

### Requirement: Batch retrieval of chat history
The server SHALL support cursor-based pagination for retrieving chat history via TRAN_GET_CHAT_HISTORY (700).

#### Scenario: Request latest messages (no cursor)
- **WHEN** a client sends TRAN_GET_CHAT_HISTORY with FIELD_CHANNEL_ID = 0 and no cursor fields
- **THEN** the server replies with up to FIELD_HISTORY_LIMIT messages (default 50) ordered oldest-first
- **AND** includes FIELD_HISTORY_HAS_MORE = 1 if older messages exist, 0 otherwise

#### Scenario: Scroll back with BEFORE cursor
- **WHEN** a client sends TRAN_GET_CHAT_HISTORY with FIELD_HISTORY_BEFORE set to a message ID
- **THEN** the server replies with up to FIELD_HISTORY_LIMIT messages with IDs strictly less than the cursor, ordered oldest-first
- **AND** includes FIELD_HISTORY_HAS_MORE = 1 if more older messages exist

#### Scenario: Catch up with AFTER cursor
- **WHEN** a client sends TRAN_GET_CHAT_HISTORY with FIELD_HISTORY_AFTER set to a message ID
- **THEN** the server replies with up to FIELD_HISTORY_LIMIT messages with IDs strictly greater than the cursor, ordered oldest-first
- **AND** includes FIELD_HISTORY_HAS_MORE = 1 if more newer messages exist

#### Scenario: Range query with both cursors
- **WHEN** a client sends TRAN_GET_CHAT_HISTORY with both FIELD_HISTORY_BEFORE and FIELD_HISTORY_AFTER set
- **THEN** the server replies with up to FIELD_HISTORY_LIMIT messages with IDs in the open interval (AFTER, BEFORE), ordered oldest-first
- **AND** includes FIELD_HISTORY_HAS_MORE = 1 if additional messages exist within the specified range beyond the returned batch

#### Scenario: Server clamps requested limit
- **WHEN** a client sends TRAN_GET_CHAT_HISTORY with FIELD_HISTORY_LIMIT exceeding the server's maximum allowed limit
- **THEN** the server silently reduces the limit to its configured maximum
- **AND** returns up to the server's maximum number of entries with no error

#### Scenario: Empty result
- **WHEN** a client sends TRAN_GET_CHAT_HISTORY and no messages match the query
- **THEN** the server replies with zero FIELD_HISTORY_ENTRY fields and FIELD_HISTORY_HAS_MORE = 0

#### Scenario: Channel 0 required
- **WHEN** a client sends TRAN_GET_CHAT_HISTORY with FIELD_CHANNEL_ID = 0
- **THEN** the server processes the request for public chat history (servers MUST support channel 0)

#### Scenario: Unsupported channel
- **WHEN** a client sends TRAN_GET_CHAT_HISTORY with a FIELD_CHANNEL_ID value the server does not recognize
- **THEN** the server replies with an error

### Requirement: History entry encoding
Each FIELD_HISTORY_ENTRY (0x0F05) SHALL contain a packed binary structure with fixed header fields followed by variable-length nickname and message data, and optional mini-TLV sub-fields for future extensibility.

#### Scenario: Standard entry parsing
- **GIVEN** a FIELD_HISTORY_ENTRY with known total data length from the TLV field header
- **WHEN** a client parses the entry
- **THEN** it reads: message_id (uint64), timestamp (int64), flags (uint16), icon_id (uint16), nick_len (uint16), nick (nick_len bytes), msg_len (uint16), message (msg_len bytes)
- **AND** if bytes remain (total length - bytes consumed > 0), the remaining bytes contain mini-TLV sub-fields

#### Scenario: Mini-TLV sub-field parsing
- **WHEN** a client encounters optional sub-fields after the message body
- **THEN** it reads each as: sub_type (uint16), sub_len (uint16), sub_data (sub_len bytes)
- **AND** it skips sub-fields with unrecognized sub_type values using sub_len to advance

#### Scenario: Tombstoned entry
- **WHEN** a FIELD_HISTORY_ENTRY has flags bit 2 (is_deleted) set
- **THEN** nick_len and msg_len MAY be zero
- **AND** message_id and timestamp are preserved
- **AND** clients display a placeholder such as "[message removed]"

#### Scenario: Text encoding matches connection
- **WHEN** the server builds FIELD_HISTORY_ENTRY fields for a client
- **THEN** nick and message text are encoded in the connection's negotiated encoding (Mac Roman or UTF-8)

### Requirement: Message ID ordering guarantee
Message IDs SHALL be opaque uint64 values with a monotonically increasing ordering guarantee. Higher IDs correspond to newer messages.

#### Scenario: ID ordering
- **GIVEN** two messages A and B where A was sent before B
- **THEN** A's message_id is strictly less than B's message_id

#### Scenario: ID opacity
- **WHEN** a client receives message IDs
- **THEN** it treats them as opaque values suitable only for ordering and cursor-based pagination
- **AND** it does not interpret, parse, or derive timestamps from ID values

### Requirement: Persistent channels separated from ephemeral private chats
FIELD_CHANNEL_ID (0x0F01) defines a persistent channel namespace that is distinct from ephemeral private chats identified by FIELD_CHAT_ID (0x0072).

#### Scenario: Channel 0 is public chat
- **WHEN** a client sends TRAN_GET_CHAT_HISTORY with FIELD_CHANNEL_ID = 0
- **THEN** the server returns history for the public chat room

#### Scenario: Channel IDs above 0 reserved for future named channels
- **WHEN** a future specification defines named persistent channels
- **THEN** they use FIELD_CHANNEL_ID values 1 and above
- **AND** they remain separate from ephemeral private chat IDs

#### Scenario: No server-side history for ephemeral chats
- **WHEN** a private chat exists using FIELD_CHAT_ID
- **THEN** the server MUST NOT persist its messages to chat history storage
- **AND** clients MAY store private chat history locally at the user's discretion

### Requirement: Access control for chat history
The server SHALL enforce access permissions for chat history retrieval.

#### Scenario: Dedicated history permission
- **WHEN** a client sends TRAN_GET_CHAT_HISTORY
- **AND** the server's access system defines ACCESS_READ_CHAT_HISTORY (bit 56)
- **THEN** the server checks bit 56 and rejects the request if the permission is not granted

#### Scenario: Fallback to chat read permission
- **WHEN** a client sends TRAN_GET_CHAT_HISTORY
- **AND** the server's access system does not define ACCESS_READ_CHAT_HISTORY (bit 56)
- **THEN** the server checks ACCESS_READ_CHAT (bit 9) as a fallback

### Requirement: Legacy broadcast for unsupported clients
The server MAY send recent chat messages to clients that do not support the chat history extension.

#### Scenario: Legacy client connects
- **WHEN** a client completes LOGIN without FIELD_CAPABILITIES bit 4
- **AND** the server has chat history and legacy broadcast enabled
- **THEN** the server MAY send recent messages as standard TRAN_CHAT_MSG (106) transactions
- **AND** the timing, quantity, and formatting of these messages is implementation-defined

### Requirement: Server retention policy
The server SHALL enforce configurable retention limits for chat history storage.

#### Scenario: Message count limit
- **WHEN** the server is configured with a maximum message count
- **AND** a new message would exceed the limit
- **THEN** the server removes the oldest message(s) to maintain the limit

#### Scenario: Time-based retention
- **WHEN** the server is configured with a maximum retention period in days
- **THEN** the server periodically removes messages older than the configured period

#### Scenario: Unlimited retention
- **WHEN** the server is configured with no retention limits (max messages = 0, max days = 0)
- **THEN** the server retains all messages indefinitely

### Requirement: Rate limiting for chat history requests
The server SHOULD enforce rate limits on TRAN_GET_CHAT_HISTORY to prevent abuse from buggy or malicious clients.

#### Scenario: Client exceeds rate limit
- **WHEN** a client sends TRAN_GET_CHAT_HISTORY requests faster than the server's configured rate limit
- **THEN** the server MAY respond with an error or silently drop excessive requests
- **AND** the specific rate limit and enforcement mechanism is implementation-defined (recommended: 10 requests per second per client)

## MODIFIED Requirements

### Requirement: Public chat broadcast to all connected users
_(From chat-messaging spec)_

No change to the broadcast behavior. When chat history is enabled, the server additionally persists the message before or after broadcasting. The broadcast path (TRAN_CHAT_MSG 106) is unchanged.
