## 1. JSONL Message Board Backend

- [x] 1.1 Create `include/mobius/jsonl_message_board.h` — struct with file path, next_id, post count; vtable-compatible with `mobius_flat_news_t` interface
- [x] 1.2 Implement `mobius_jsonl_news_new()` — open or create `MessageBoard.jsonl`, scan for max ID
- [x] 1.3 Implement JSONL append — write `{id, nick, login, body, ts}` as one line, increment next_id
- [x] 1.4 Implement JSONL read-all — parse all lines, return formatted text for Hotline wire protocol (delimiter-separated, newest first)
- [x] 1.5 Implement post count — count lines in file (for heartbeat)
- [x] 1.6 Implement `mobius_jsonl_news_free()` — cleanup (reuses mobius_flat_news_free)

## 2. Message Board Migration

- [x] 2.1 Implement flat text parser — split `MessageBoard.txt` on delimiter, extract nick/date from header lines where possible
- [x] 2.2 Implement migration writer — convert parsed posts to JSONL format with generated IDs and timestamps
- [x] 2.3 Implement auto-detection on startup — check for `MessageBoard.jsonl` vs `MessageBoard.txt`, migrate if needed, rename old file to `.legacy`
- [x] 2.4 Wire migration into `main.c` — call detection/migration before creating message board manager

## 3. Directory Threaded News Backend

- [x] 3.1 Create `include/mobius/dir_threaded_news.h` — struct with base path, category list; vtable-compatible with `mobius_threaded_news_t` interface
- [x] 3.2 Implement `mobius_dir_news_new()` — scan `News/` directory, load `_meta.json` for each category/bundle, count articles
- [x] 3.3 Implement category listing — enumerate subdirectories, read `_meta.json`, return Hotline-format category list
- [x] 3.4 Implement article listing — read numbered JSON files from category directory, return Hotline-format article list
- [x] 3.5 Implement get article — read single `NNNN.json`, return title/poster/date/body
- [x] 3.6 Implement post article — auto-increment ID, write `NNNN.json` with ISO timestamp
- [x] 3.7 Implement delete article — remove `NNNN.json` file
- [x] 3.8 Implement create category — mkdir + write `_meta.json` with random GUID
- [x] 3.9 Implement delete category — remove directory recursively
- [x] 3.10 Implement bundle support — nested directories, `kind: "bundle"` in `_meta.json`
- [x] 3.11 Implement `mobius_dir_news_free()` — cleanup (reuses mobius_threaded_news_free)

## 4. Threaded News Migration

- [x] 4.1 Implement YAML-to-directory converter — read `ThreadedNews.yaml`, create `News/` tree with `_meta.json` and article files
- [x] 4.2 Convert Hotline 8-byte dates to ISO 8601 during migration
- [x] 4.3 Preserve category GUIDs from YAML `guid` arrays
- [x] 4.4 Implement auto-detection on startup — check for `News/` vs `ThreadedNews.yaml`, migrate if needed, rename old file to `.legacy`
- [x] 4.5 Wire migration into `main.c`

## 5. Mnemosyne Message Board Sync

- [x] 5.1 Add `index_msgboard` config field (default: true) and parse from YAML
- [x] 5.2 Implement `mn_build_msgboard_json()` — `{mode: "full", posts: [{id, nick, body, timestamp}]}`
- [x] 5.3 Add full msgboard sync to `mn_start_full_sync()` — POST to `/api/v1/sync/msgboard` when JSONL backend is active
- [ ] 5.4 Add incremental msgboard hook — queue new posts for `POST /api/v1/sync/msgboard` with `mode: "incremental"`
- [x] 5.5 Update heartbeat to include actual `msgboard_posts` count when JSONL is active

## 6. Integration & Testing

- [x] 6.1 Wire format detection into `main.c` — pick JSONL or flat text for message board, directory or YAML for news
- [ ] 6.2 Write unit tests for JSONL message board (append, read, count, empty)
- [ ] 6.3 Write unit tests for directory news (create category, post article, delete, list)
- [ ] 6.4 Write unit tests for message board migration (flat text → JSONL)
- [ ] 6.5 Write unit tests for news migration (YAML → directory)
- [ ] 6.6 Write unit test for msgboard Mnemosyne JSON builder
- [x] 6.7 Update Makefile with new source files
- [x] 6.8 Verify clean build on macOS and Linux (Docker)
- [ ] 6.9 Test against live server — migrate existing Apple Media Archive config, verify clients can read news and message board
- [ ] 6.10 Test Mnemosyne msgboard sync — verify posts appear in `agora.vespernet.net/search`
