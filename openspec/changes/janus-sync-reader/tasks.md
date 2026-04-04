## 0. Cleanup Dead Includes

- [x] 0.1 Remove unused `#include "mobius/jsonl_message_board.h"` and `#include "mobius/dir_threaded_news.h"` from `main.c`
- [x] 0.2 Remove unused `#include "mobius/dir_threaded_news.h"` and `#include <sys/stat.h>` from `threaded_news_yaml.c`
- [x] 0.3 Verify build — no client-path code references Janus modules

## 1. Flat Text Message Board Parser

- [x] 1.1 Implement `mn_parse_flat_msgboard()` — split `MessageBoard.txt` on delimiter, extract nick/date/body per post, return array of structured posts
- [x] 1.2 Handle header parsing edge cases — posts without `"nick (date)"` format, empty posts, posts with only body text
- [x] 1.3 Generate sequential post IDs and use file mtime as fallback timestamp
- [x] 1.4 Add post count function — count delimiters in flat text for heartbeat (fast, no full parse)

## 2. Janus Format Readers (Optional Sources)

- [ ] 2.1 Wire existing `jsonl_message_board.c` as read-only sync source — use `mobius_jsonl_get_posts()` when `MessageBoard.jsonl` exists (deferred — flat text parser works)
- [ ] 2.2 Wire existing `dir_threaded_news.c` as read-only sync source — use `mobius_dir_news_new()` when `News/` directory exists (deferred — YAML in-memory works)
- [ ] 2.3 Add sync-time format detection — check for JSONL/News dir before falling back to flat text/YAML parsing (deferred)

## 3. Mnemosyne Message Board Sync

- [x] 3.1 Implement `do_full_msgboard_sync()` — parse posts (from JSONL or flat text), build `{mode: "full", posts: [...]}` JSON, POST to `/api/v1/sync/msgboard`
- [x] 3.2 Add msgboard to `mn_start_full_sync()` — call after files and news (already wired from previous session)
- [x] 3.3 Update heartbeat to include `msgboard_posts` count from flat text delimiter count or JSONL line count
- [ ] 3.4 Add incremental msgboard hook in `handle_old_post_news` — extract nick/body from the prepend data, queue for Mnemosyne

## 4. Testing

- [ ] 4.1 Write unit test for flat text parser — known input with multiple posts, verify extracted nick/body/timestamp
- [ ] 4.2 Write unit test for flat text parser edge cases — no header, empty board, single post
- [ ] 4.3 Write unit test for msgboard sync JSON builder — verify output matches fogWraith's API spec
- [ ] 4.4 Verify existing unit tests still pass (28/28)
- [x] 4.5 Build and deploy to VPS — verify msgboard posts appear on agora.vespernet.net/search
- [x] 4.6 Verify clean build on macOS and Linux (Docker)
