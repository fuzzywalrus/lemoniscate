## ADDED Requirements

### Requirement: Heartbeat to Mnemosyne instance
The server SHALL send a heartbeat POST to `/api/v1/sync/heartbeat` every 5 minutes when Mnemosyne sync is configured. The heartbeat SHALL include the server name, description, and content counts (message board posts, file count, news article count). Authentication SHALL use the `?api_key=msv_...` query parameter.

#### Scenario: Heartbeat sent on interval
- **WHEN** the 5-minute heartbeat timer fires and Mnemosyne is configured
- **THEN** the server POSTs `{server_name, description, post_count, file_count, news_count}` to `/api/v1/sync/heartbeat`

#### Scenario: Heartbeat with no content indexed
- **WHEN** all content types are disabled
- **THEN** the heartbeat sends zero counts for all content types

#### Scenario: Heartbeat failure
- **WHEN** the heartbeat POST fails (network error or non-200 status)
- **THEN** the server logs a warning and continues operating normally

### Requirement: Chunked full sync for files
The server SHALL perform a chunked full sync of file listings via `POST /api/v1/sync/files/chunked`. The sync walks the file directory tree one directory per chunk tick (2 seconds). Each chunk includes a `sync_id`, `chunk_index`, and a `finalize` flag. The final chunk has `finalize: true` and an empty files array.

#### Scenario: File sync walks directory tree
- **WHEN** a file sync starts
- **THEN** the server walks the file root recursively, sending one directory's files per chunk POST

#### Scenario: File sync skips drop box directories
- **WHEN** a directory has drop box access restrictions
- **THEN** it is skipped during file sync

#### Scenario: File sync finalize
- **WHEN** all directories have been processed
- **THEN** the server sends a final chunk with `finalize: true` and an empty files array

#### Scenario: File chunk payload
- **WHEN** a directory contains files
- **THEN** each file is serialized as `{path, name, size, type, comment}`

### Requirement: Message board sync deferred
Message board sync is NOT implemented in this change. The flat message board (`MessageBoard.txt`) stores posts as raw unstructured text without post IDs, pagination, or deletion support. The Mnemosyne API expects structured posts. A future change can add a message board parser to enable msgboard sync. The heartbeat SHALL report `post_count` as 0.

#### Scenario: Message board not synced
- **WHEN** a full sync runs
- **THEN** message board sync is skipped regardless of the `index_msgboard` config toggle

### Requirement: Chunked full sync for threaded news
The server SHALL perform a chunked full sync of threaded news via `POST /api/v1/sync/news/chunked`. The first chunk sends categories. Subsequent chunks send articles per category. The final chunk has `finalize: true`.

#### Scenario: News categories sent first
- **WHEN** a news sync starts
- **THEN** the first chunk contains all categories as `{name, path}` with an empty articles array

#### Scenario: News articles sent per category
- **WHEN** a category has articles
- **THEN** each article is serialized as `{id, path, title, body, poster, date}`

#### Scenario: News sync finalize
- **WHEN** all categories and articles have been sent
- **THEN** the server sends a final chunk with `finalize: true`

### Requirement: Full sync sequencing
The server SHALL sync content types one at a time in sequence: files, then news. When one type completes (finalize acknowledged), the next type begins.

#### Scenario: Sync queue
- **WHEN** a full sync starts with both content types enabled
- **THEN** file sync runs first, then news sync

#### Scenario: Single content type
- **WHEN** only files is enabled
- **THEN** only file sync runs

### Requirement: Incremental sync on content changes
The server SHALL push incremental updates immediately when content changes. Incremental POSTs use `{mode: "incremental", added: [...], removed: [...]}`. Message board incremental sync is deferred (see "Message board sync deferred" requirement).

#### Scenario: File uploaded
- **WHEN** a file upload completes
- **THEN** the server POSTs `{mode: "incremental", added: [{path, name, size, type}]}` to `/api/v1/sync/files`

#### Scenario: File deleted
- **WHEN** a file is deleted
- **THEN** the server POSTs `{mode: "incremental", removed: ["/path/to/file"]}` to `/api/v1/sync/files`

#### Scenario: News article posted
- **WHEN** a news article is posted
- **THEN** the server POSTs `{mode: "incremental", added: [{id, path, title, body, poster, date}]}` to `/api/v1/sync/news`

### Requirement: Periodic drift detection
The server SHALL check for content drift every 15 minutes by comparing cached content counts with actual counts. If a mismatch is detected, a targeted full sync SHALL be triggered.

#### Scenario: Drift detected
- **WHEN** the actual content count differs from the cached count for any content type
- **THEN** the server triggers a targeted full sync for that content type

#### Scenario: No drift
- **WHEN** all counts match
- **THEN** no sync is triggered

### Requirement: Sync state persistence
The server SHALL persist the sync cursor to disk after each chunk. On startup, if an interrupted sync cursor exists, the server SHALL resume from where it left off.

#### Scenario: Sync interrupted by crash
- **WHEN** the server crashes during a chunked sync
- **THEN** on restart, the server resumes from the persisted cursor

#### Scenario: Clean startup
- **WHEN** no persisted sync cursor exists
- **THEN** a full sync begins after a 30-second startup delay

### Requirement: Deregistration on shutdown
The server SHALL send `POST /api/v1/sync/deregister` when shutting down gracefully.

#### Scenario: Graceful shutdown
- **WHEN** the server receives SIGTERM/SIGINT and Mnemosyne is configured
- **THEN** it sends a deregister POST before exiting

### Requirement: API key authentication via query parameter
The server SHALL authenticate all Mnemosyne sync requests using the `?api_key=msv_...` query parameter appended to each POST URL.

#### Scenario: API key on every request
- **WHEN** any POST is sent to Mnemosyne
- **THEN** the URL includes `?api_key=<configured_key>`

#### Scenario: Missing API key
- **WHEN** Mnemosyne URL is configured but API key is missing
- **THEN** sync is disabled with a log warning

### Requirement: Exponential backoff on failure
The server SHALL apply exponential backoff when Mnemosyne requests fail. After 5 consecutive failures, all sync activity SHALL be suspended. The heartbeat timer SHALL continue as a health probe. When a heartbeat succeeds, sync SHALL resume with a fresh full sync.

#### Scenario: First failure
- **WHEN** a Mnemosyne POST fails for the first time
- **THEN** the server logs a warning and retries on the next tick

#### Scenario: Escalating backoff
- **WHEN** consecutive failures occur
- **THEN** retry intervals double (2s, 4s, 8s, 16s, 32s) and only one log line is emitted per escalation level

#### Scenario: Sync suspension after 5 failures
- **WHEN** 5 consecutive Mnemosyne requests fail
- **THEN** the chunk tick timer is stopped, the incremental queue is drained, and the server logs "sync suspended, will resume when heartbeat succeeds"

#### Scenario: Recovery via heartbeat
- **WHEN** a heartbeat succeeds while sync is suspended
- **THEN** backoff is reset, sync resumes with a fresh full sync, and the server logs "Mnemosyne reachable again, resuming sync"

### Requirement: Queued incremental sync
The server SHALL queue incremental sync events in a bounded ring buffer (64 entries) and drain them on the next chunk tick or heartbeat, not inline during the transaction handler. If the queue is full, the oldest entry SHALL be dropped.

#### Scenario: Incremental event queued
- **WHEN** a user uploads a file while Mnemosyne is reachable
- **THEN** the file metadata is added to the incremental queue and sent on the next timer tick, not during the upload handler

#### Scenario: Queue overflow
- **WHEN** the incremental queue is full (64 entries)
- **THEN** the oldest entry is dropped and a debug log is emitted

#### Scenario: Queue drained while sync suspended
- **WHEN** incremental events queue up while sync is suspended
- **THEN** the queue entries are discarded on suspension and the next full sync will capture all changes

### Requirement: DNS caching
The server SHALL resolve the Mnemosyne hostname once on startup and cache the IP address. The cached address SHALL be refreshed on SIGHUP config reload and after recovery from sync suspension.

#### Scenario: DNS resolved once
- **WHEN** the server starts with Mnemosyne configured
- **THEN** the Mnemosyne hostname is resolved to an IP address and cached for all subsequent requests

#### Scenario: DNS failure on startup
- **WHEN** the Mnemosyne hostname cannot be resolved on startup
- **THEN** sync is disabled with a log warning

### Requirement: Tiered logging
The server SHALL log Mnemosyne sync events at appropriate levels without spamming. First failure logs at WARN. Backoff escalations log once per level. Suspension and recovery log once each. Routine heartbeat failures during suspension SHALL NOT be logged.

#### Scenario: Normal sync logging
- **WHEN** a full sync completes successfully
- **THEN** the server logs one INFO line per completed content type and one INFO line for "full sync complete"

#### Scenario: Suspended state logging
- **WHEN** sync is suspended and heartbeat fails
- **THEN** no log line is emitted (already logged suspension)

#### Scenario: Recovery logging
- **WHEN** heartbeat succeeds after suspension
- **THEN** one INFO line: "Mnemosyne reachable again, resuming sync"

### Requirement: Resource protection
The server SHALL bound all sync-related memory allocations. JSON buffers are allocated per-chunk and freed after POST. The pending directory list is bounded by filesystem depth. The incremental queue is a fixed-size ring buffer. Stale sync cursors older than 1 hour SHALL be discarded on startup.

#### Scenario: Stale cursor discarded
- **WHEN** the server starts and finds a sync cursor file older than 1 hour
- **THEN** the cursor is discarded and a fresh full sync begins

#### Scenario: JSON buffer freed after POST
- **WHEN** a chunk POST completes (success or failure)
- **THEN** the JSON buffer for that chunk is freed immediately
