## Why

Mnemosyne message board sync is currently impossible because `MessageBoard.txt` stores posts as raw unstructured text with no IDs, timestamps, or author fields. The Mnemosyne API expects `{id, nick, body, timestamp}` per post. Additionally, servers running Janus (fogWraith's Hotline server) store news and message boards in a structured JSON format that Lemoniscate can't read.

An earlier attempt to replace the flat text and YAML backends with Janus-format storage broke the Hotline wire protocol for clients. The correct approach: keep the existing backends for all client-facing operations and add read-only parsers that extract structured data for Mnemosyne sync only.

## What Changes

- Add a flat text message board parser that extracts structured posts from `MessageBoard.txt` at Mnemosyne sync time — parsing the delimiter-separated format to extract nick, date, and body per post.
- Add optional Janus format readers: if `MessageBoard.jsonl` or `News/` directory exist alongside the standard files, read structured data from those for Mnemosyne sync instead of parsing flat text/YAML.
- Enable Mnemosyne message board sync: `POST /api/v1/sync/msgboard` with `{mode: "full", posts: [...]}`.
- Add incremental msgboard sync hook: when a user posts to the board, queue the structured post for Mnemosyne.
- Update heartbeat to include actual `msgboard_posts` count.
- **No changes to client-facing backends**: `flat_news.c` and `threaded_news_yaml.c` are untouched. Clients continue to use `MessageBoard.txt` and `ThreadedNews.yaml` for all Hotline protocol operations.

## Capabilities

### New Capabilities
- `sync-content-reader`: Read-only parsers that extract structured content from existing storage for Mnemosyne sync. Covers: flat text message board parsing, Janus JSONL message board reading, Janus directory news reading. These are sync-time readers, not storage backends.

### Modified Capabilities
- `mnemosyne-sync`: Enable message board sync (currently deferred). Add `POST /api/v1/sync/msgboard` full and incremental modes. Update heartbeat `counts.msgboard_posts`.

## Impact

- **Affected code**: `mnemosyne_sync.c` (add msgboard sync, update heartbeat), new parser functions in a helper module.
- **NOT affected**: `flat_news.c`, `flat_news.h`, `threaded_news_yaml.c`, `threaded_news_yaml.h` — client-facing backends are completely untouched.
- **Existing code reuse**: The `jsonl_message_board.c` and `dir_threaded_news.c` modules already exist in the codebase (from the archived `janus-news-storage` change). They can be used as read-only helpers for Mnemosyne sync without being wired into the client path.
- **Config**: `index_msgboard` already added to Mnemosyne YAML section (default true).
