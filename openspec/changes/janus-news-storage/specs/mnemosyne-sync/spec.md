## MODIFIED Requirements

### Requirement: Message board sync deferred
Message board sync is NOT implemented in this change. The flat message board (`MessageBoard.txt`) stores posts as raw unstructured text without post IDs, pagination, or deletion support. The Mnemosyne API expects structured posts. A future change can add a message board parser to enable msgboard sync. The heartbeat SHALL report `post_count` as 0.

**This requirement is replaced by the following:**

### Requirement: Message board sync via JSONL
The server SHALL sync message board posts to Mnemosyne when `index_msgboard` is enabled (default: true) and the message board uses JSONL storage. The sync SHALL use `POST /api/v1/sync/msgboard` with `{mode: "full", posts: [{id, nick, login, body, timestamp}]}`.

#### Scenario: Full message board sync
- **WHEN** a full sync runs and `index_msgboard` is enabled
- **THEN** the server SHALL POST all message board posts from `MessageBoard.jsonl` to `/api/v1/sync/msgboard` with `mode: "full"`

#### Scenario: Incremental message board sync
- **WHEN** a user posts to the message board and Mnemosyne sync is active
- **THEN** the server SHALL queue an incremental sync with `{mode: "incremental", added: [{id, nick, body, timestamp}]}`

#### Scenario: Message board sync with flat text fallback
- **WHEN** the message board is still in flat text format (not yet migrated)
- **THEN** message board sync SHALL be skipped and the heartbeat SHALL report `msgboard_posts: 0`

#### Scenario: Heartbeat includes message board count
- **WHEN** a heartbeat is sent and the message board uses JSONL
- **THEN** the `counts.msgboard_posts` field SHALL reflect the actual number of posts in `MessageBoard.jsonl`
