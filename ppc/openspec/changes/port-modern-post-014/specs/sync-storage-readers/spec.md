## ADDED Requirements

### Requirement: Sync-only directory-threaded-news reader
The server SHALL include a `dir_threaded_news.c` module that reads threaded news stored in filesystem directories. This module is consumed EXCLUSIVELY by the Mnemosyne sync subsystem. It MUST NOT be registered as a client-facing backend or referenced by any transaction handler.

#### Scenario: Mnemosyne reads news for sync
- **WHEN** the Mnemosyne sync subsystem performs a full or incremental sync of news content
- **THEN** the sync reader SHALL parse directory-based news into structured data for JSON serialization to the Mnemosyne API

#### Scenario: Client requests news articles
- **WHEN** a Hotline client requests threaded news articles via the wire protocol
- **THEN** the request SHALL be served by `threaded_news_yaml.c` (the existing client-facing backend), NOT by `dir_threaded_news.c`

#### Scenario: No directory news data exists
- **WHEN** the sync reader is invoked but no directory-format news data exists on disk
- **THEN** the reader SHALL return an empty result without error, and Mnemosyne sync SHALL fall back to parsing the YAML news files directly

### Requirement: Sync-only JSONL message board reader
The server SHALL include a `jsonl_message_board.c` module that reads message board posts from JSONL files. This module is consumed EXCLUSIVELY by the Mnemosyne sync subsystem. It MUST NOT be registered as a client-facing backend or referenced by any transaction handler.

#### Scenario: Mnemosyne reads message board for sync
- **WHEN** the Mnemosyne sync subsystem performs a full or incremental sync of message board content
- **THEN** the sync reader SHALL parse JSONL posts into structured data for JSON serialization to the Mnemosyne API

#### Scenario: Client requests message board
- **WHEN** a Hotline client requests message board contents via the wire protocol
- **THEN** the request SHALL be served by `flat_news.c` (the existing client-facing backend), NOT by `jsonl_message_board.c`

#### Scenario: No JSONL data exists — fall back to flat text
- **WHEN** the sync reader is invoked but no JSONL message board file exists
- **THEN** the reader SHALL fall back to best-effort parsing of the flat text `MessageBoard.txt`, extracting nick, date, and body from delimiter-separated entries

### Requirement: JSON builder utility
The server SHALL provide a `json_builder.c` utility for constructing JSON objects and arrays without external dependencies, used by both sync readers and Mnemosyne sync for content serialization.

#### Scenario: Build JSON object with special characters
- **WHEN** code constructs a JSON object containing strings with quotes, backslashes, or control characters
- **THEN** the builder SHALL produce valid JSON with proper escaping

#### Scenario: Build nested JSON structures
- **WHEN** code constructs a JSON object containing nested objects or arrays
- **THEN** the builder SHALL produce correctly nested, valid JSON output

### Requirement: Client-facing backends remain untouched
The existing client-facing backends (`flat_news.c`, `threaded_news_yaml.c`) SHALL NOT have their wire-format behavior altered by this change. All Hotline client operations (read, write, delete for news and message board) SHALL continue to use these backends exclusively.

#### Scenario: Flat news wire format unchanged
- **WHEN** a Hotline client reads or writes to the message board after this change is deployed
- **THEN** the wire bytes produced SHALL be identical to those produced by the pre-change `flat_news.c`

#### Scenario: YAML threaded news wire format unchanged
- **WHEN** a Hotline client reads or posts threaded news articles after this change is deployed
- **THEN** the wire bytes produced SHALL be identical to those produced by the pre-change `threaded_news_yaml.c` (except for the UTF-8/round-trip bugfixes which are a separate capability)
