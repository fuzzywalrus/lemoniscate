## ADDED Requirements

### Requirement: TCP handshake completes protocol negotiation
The server SHALL accept client connections on the configured protocol port (default 5500) and perform the Hotline handshake. The client sends a 12-byte handshake: TRTP(4) + HOTL(4) + Version(2) + SubVersion(2). The server SHALL validate the protocol magic bytes (TRTP and HOTL) and reply with an 8-byte response: TRTP(4) + ErrorCode(4), where ErrorCode is 0x00000000 on success.

#### Scenario: Successful handshake
- **WHEN** a client connects and sends the 12-byte handshake with valid TRTP and HOTL magic bytes
- **THEN** the server replies with TRTP + 0x00000000 (success) and the connection enters the transaction phase

#### Scenario: Invalid protocol magic
- **WHEN** a client sends a handshake where the first 4 bytes are not "TRTP" or bytes 5-8 are not "HOTL"
- **THEN** the server closes the connection

### Requirement: Transaction framing follows the Hotline wire format
The server SHALL serialize and deserialize transactions using the 22-byte header format: Flags(1) + IsReply(1) + Type(2) + ID(4) + ErrorCode(4) + TotalSize(4) + DataSize(4) + ParamCount(2), followed by variable-length fields.

#### Scenario: Transaction deserialization
- **WHEN** the server receives a byte buffer containing a complete transaction (header + declared fields)
- **THEN** it parses the header, extracts all fields by type and length, and dispatches to the registered handler for the transaction type

#### Scenario: Incomplete transaction buffering
- **WHEN** a byte buffer contains fewer bytes than the header's declared TotalSize
- **THEN** the server buffers the data and waits for additional bytes before parsing

### Requirement: Fields use type-length-value encoding
Each transaction field SHALL be encoded as FieldType(2) + FieldSize(2) + FieldData(variable), with all multi-byte integers in big-endian byte order.

#### Scenario: Field serialization round-trip
- **WHEN** a field is serialized to wire bytes and then deserialized
- **THEN** the resulting field type, size, and data are identical to the original

#### Scenario: Field lookup by type
- **WHEN** a transaction contains multiple fields and a handler requests a specific field type
- **THEN** the server returns the first field matching that type, or NULL if not present

### Requirement: Multi-byte integers use big-endian encoding
All integer values in the wire protocol (field types, field sizes, transaction IDs, error codes, user IDs, etc.) SHALL be encoded in network byte order (big-endian).

#### Scenario: 16-bit integer encoding
- **WHEN** a uint16 value is written to the wire
- **THEN** the most significant byte appears first

#### Scenario: 32-bit integer encoding
- **WHEN** a uint32 value (e.g., transaction ID or total size) is written to the wire
- **THEN** the four bytes appear in big-endian order

### Requirement: Transaction type dispatches to registered handlers
The server SHALL maintain a handler dispatch table indexed by transaction type code. Each incoming transaction SHALL be routed to the handler registered for its type.

#### Scenario: Known transaction type
- **WHEN** a transaction arrives with a type code that has a registered handler
- **THEN** the server invokes that handler with the transaction and client connection context

#### Scenario: Unknown transaction type
- **WHEN** a transaction arrives with a type code that has no registered handler
- **THEN** the server ignores the transaction without crashing or disconnecting the client

### Requirement: Protocol version is 190
The server SHALL identify itself as Hotline protocol version 190 (0x00BE) in tracker registrations and version-reporting contexts, ensuring compatibility with Hotline 1.8+ clients.

#### Scenario: Version reported to tracker
- **WHEN** the server registers with a tracker
- **THEN** the protocol version field is set to 190

### Requirement: Text encoding supports MacRoman and UTF-8
The server SHALL support configurable text encoding. When set to "macintosh", string fields are treated as MacRoman. When set to "utf-8", strings are treated as UTF-8. The encoding applies to chat messages, user names, file names, and news content.

#### Scenario: MacRoman encoding configured
- **WHEN** the server config specifies encoding "macintosh"
- **THEN** text fields are interpreted and sent as MacRoman-encoded strings

#### Scenario: UTF-8 encoding configured
- **WHEN** the server config specifies encoding "utf-8"
- **THEN** text fields are interpreted and sent as UTF-8-encoded strings
