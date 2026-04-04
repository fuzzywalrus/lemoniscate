## MODIFIED Requirements

### Requirement: Message board sync deferred
Message board sync is NOT implemented in this change. The flat message board (`MessageBoard.txt`) stores posts as raw unstructured text without post IDs, pagination, or deletion support. The Mnemosyne API expects structured posts. A future change can add a message board parser to enable msgboard sync. The heartbeat SHALL report `post_count` as 0.

**This requirement is replaced by the following:**

### Requirement: Message board sync via parsed content
The server SHALL sync message board posts to Mnemosyne when `index_msgboard` is enabled. Posts SHALL be parsed from `MessageBoard.txt` (or read from `MessageBoard.jsonl` if present) at sync time. The sync SHALL use `POST /api/v1/sync/msgboard`.

#### Scenario: Full message board sync
- **WHEN** a full sync runs and `index_msgboard` is enabled
- **THEN** the server SHALL parse all posts and POST to `/api/v1/sync/msgboard` with `{mode: "full", posts: [{id, nick, body, timestamp}]}`

#### Scenario: Incremental message board sync
- **WHEN** a user posts to the message board and Mnemosyne sync is active
- **THEN** the server SHALL extract the structured post data from the prepend and queue an incremental sync with `{mode: "incremental", added: [{id, nick, body, timestamp}]}`

#### Scenario: Heartbeat includes message board count
- **WHEN** a heartbeat is sent
- **THEN** `counts.msgboard_posts` SHALL reflect the actual post count (parsed from delimiter count in flat text, or line count in JSONL)
