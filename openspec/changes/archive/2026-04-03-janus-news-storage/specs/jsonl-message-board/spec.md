## ADDED Requirements

### Requirement: JSONL message board storage
The server SHALL store message board posts in `MessageBoard.jsonl` format, with one JSON object per line containing `id` (integer), `nick` (string), `login` (string), `body` (string), and `ts` (ISO 8601 timestamp).

#### Scenario: New post appended
- **WHEN** a user posts to the message board
- **THEN** a new JSON line SHALL be appended to `MessageBoard.jsonl` with an auto-incremented `id`, the user's nick and login, the post body, and the current UTC timestamp

#### Scenario: Read all posts
- **WHEN** the message board is read
- **THEN** all posts SHALL be returned in order (newest first for display, matching Hotline convention)

#### Scenario: Empty message board
- **WHEN** `MessageBoard.jsonl` does not exist or is empty
- **THEN** the server SHALL return an empty message board

### Requirement: Migration from flat text format
The server SHALL auto-migrate from `MessageBoard.txt` to `MessageBoard.jsonl` on startup when the JSONL file does not exist but the text file does.

#### Scenario: Auto-migration on first startup
- **WHEN** the config directory contains `MessageBoard.txt` but not `MessageBoard.jsonl`
- **THEN** the server SHALL parse the text file into structured posts, write `MessageBoard.jsonl`, and rename the old file to `MessageBoard.txt.legacy`

#### Scenario: Migration preserves content
- **WHEN** migrating from flat text
- **THEN** each post SHALL retain its body text. Nicks and timestamps SHALL be extracted from header lines where possible, with fallback to "unknown" and the file modification time.

#### Scenario: Already migrated
- **WHEN** `MessageBoard.jsonl` already exists
- **THEN** the server SHALL use it directly without touching `MessageBoard.txt`

### Requirement: Wire protocol compatibility
The message board SHALL continue to work with all existing Hotline clients. The JSONL storage is an internal detail — clients see the same text-formatted message board via the standard Hotline protocol.

#### Scenario: Client reads message board
- **WHEN** a client requests the message board
- **THEN** the server SHALL format JSONL posts into the traditional text format with delimiter-separated entries (same as the old flat text output)

#### Scenario: Client posts to message board
- **WHEN** a client sends a message board post
- **THEN** the server SHALL extract the body, create a structured JSONL entry, and append it
