## Why

Mnemosyne message board sync is blocked because `MessageBoard.txt` stores posts as raw unstructured text — no IDs, no timestamps, no author fields. The Mnemosyne API expects `{id, nick, body, timestamp}` per post. Rather than replacing the client-facing flat text backend (which produces the correct Hotline wire format), we need to parse the flat text into structured data at sync time only.

**Lesson learned:** An earlier attempt replaced the flat text and YAML backends with Janus-compatible formats (JSONL, directory tree). This broke the Hotline wire protocol for clients — message boards showed "unknown (unknown date)" and news lost articles. The correct approach: keep existing backends for clients, parse into structured data only for Mnemosyne.

## What Changes

- Add a flat text parser that extracts structured posts from `MessageBoard.txt` at Mnemosyne sync time. Each post is delimited by the separator line (`_____...`), and the header `"nick (date)\r\r"` is parsed for author and timestamp.
- Enable Mnemosyne message board sync: `POST /api/v1/sync/msgboard` with `{mode: "full", posts: [{id, nick, body, timestamp}]}`.
- Add incremental msgboard sync hook: when a user posts to the board, queue the new post for Mnemosyne.
- Update heartbeat to include `msgboard_posts` count (parsed from flat text line count or delimiter count).
- Optionally read Janus `MessageBoard.jsonl` for sync if present alongside `MessageBoard.txt` — but never use it for the client-facing backend.

## Capabilities

### New Capabilities
- `msgboard-text-parser`: Parse `MessageBoard.txt` flat text into structured posts for Mnemosyne sync. Best-effort extraction of nick, date, and body from the delimiter-separated format. Posts without parseable headers get "unknown" author and file modification timestamp.

### Modified Capabilities
- `mnemosyne-sync`: Enable message board sync (currently deferred). Add `POST /api/v1/sync/msgboard` full and incremental modes. Update heartbeat `counts.msgboard_posts`.

## Impact

- **Affected code**: `mnemosyne_sync.c` (add msgboard sync function, update heartbeat), new parser function (can live in `mnemosyne_sync.c` or a small helper).
- **NOT affected**: `flat_news.c`, `flat_news.h`, `threaded_news_yaml.c` — client-facing backends are untouched.
- **Config**: Add `index_msgboard` to Mnemosyne YAML section (already added in 0.1.5, default true).
- **Data files**: No new files. Reads existing `MessageBoard.txt` at sync time.

**Note:** The JSONL message board and directory news code from the archived `janus-news-storage` change remain in the codebase as unused modules. They can be cleaned up or repurposed later if a proper backend swap is needed (with correct wire format support).
