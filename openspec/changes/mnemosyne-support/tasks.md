## 1. Configuration

- [x] 1.1 Add Mnemosyne config fields to `hl_config_t` — URL string, API key string (`msv_`), index_files bool, index_news bool
- [x] 1.2 Add `mnemosyne` section parsing to `config_loader.c` — parse `url`, `api_key`, `index_files`, `index_news` from YAML
- [x] 1.3 Add required field validation — disable sync and warn if `url` present but `api_key` missing
- [x] 1.4 Add SIGHUP reload support for Mnemosyne config fields
- [x] 1.5 Add Mnemosyne section to `config.yaml.example` with documented options

## 2. HTTP Client (if not already available from platform work)

- [x] 2.1 Create `include/hotline/http_client.h` — POST function with URL, body, content-type, and response status
- [x] 2.2 Implement `src/hotline/http_client.c` — TCP connect with 5s timeout, send HTTP/1.1 POST, read response status
- [x] 2.3 Implement URL parsing (extract host, port, path) and `?api_key=` query parameter appending
- [x] 2.4 Implement 10-second read timeout via `SO_RCVTIMEO`

## 3. JSON Utilities

- [x] 3.1 Create JSON string escaping utility — handle quotes, backslashes, newlines, carriage returns, tabs, control characters
- [x] 3.2 Create dynamic buffer type for building JSON payloads (growable char buffer with append/printf helpers)

## 4. Content Serialization

- [x] 4.1 Create `include/mobius/mnemosyne_sync.h` with sync state types, function declarations, and content type flags
- [x] 4.2 Implement heartbeat JSON builder — `{server_name, description, post_count, file_count, news_count}`
- [x] 4.3 Implement file chunk JSON builder — `{sync_id, chunk_index, finalize, files: [{path, name, size, type, comment}]}`
- [x] 4.4 Implement news chunk JSON builder — `{sync_id, chunk_index, finalize, categories: [{name, path}], articles: [{id, path, title, body, poster, date}]}`
- [x] 4.5 Implement incremental file JSON builder — `{mode: "incremental", added: [...], removed: [...]}`
- [x] 4.6 Implement incremental news JSON builder — `{mode: "incremental", added: [...]}`

## 5. Sync State Machine

- [x] 5.1 Define sync cursor struct — type (files/msgboard/news), sync_id, chunk_index, position data (pending_dirs for files, offset for msgboard, cat_index for news)
- [x] 5.2 Implement sync_id generation (timestamp + random)
- [x] 5.3 Implement file sync state machine — `start_file_sync`, `do_file_sync_tick` (walk one directory per tick, skip drop boxes, finalize when done)
- [x] 5.4 Implement news sync state machine — `start_news_sync`, `do_news_sync_tick` (categories first, then articles per category, finalize when done)
- [x] 5.5 Implement sync queue — chain files → news, start next type when current finalizes
- [x] 5.7 Implement `start_full_sync` — build queue from enabled content types, start first
- [x] 5.8 Implement `start_targeted_sync(type)` — single content type resync for drift correction

## 6. Sync Lifecycle

- [x] 6.1 Implement `send_heartbeat` — POST to `/api/v1/sync/heartbeat` with cached counts
- [x] 6.2 Implement `periodic_check` — compare cached counts vs actual, trigger targeted sync on drift
- [x] 6.3 Implement sync state persistence — write cursor to file after each chunk, read on startup
- [x] 6.4 Implement sync resume on startup — if persisted cursor exists, resume chunked sync
- [x] 6.5 Implement 30-second startup delay before first full sync
- [x] 6.6 Implement `deregister` — POST to `/api/v1/sync/deregister` on graceful shutdown

## 7. Incremental Sync Hooks

- [x] 7.1 Hook file upload complete — trigger incremental file sync with added file
- [x] 7.2 Hook file delete handler — trigger incremental file sync with removed path
- [x] 7.3 Hook file move handler — trigger periodic_check (full resync if drift)
- [x] 7.4 Hook news article post — trigger incremental news sync with added article

## 8. Resilience & Protection

- [x] 8.1 Implement exponential backoff counter — track consecutive failures, compute next retry delay (2s, 4s, 8s, 16s, 32s)
- [x] 8.2 Implement sync suspension — after 5 consecutive failures, stop chunk tick timer, clear incremental queue, set state to SUSPENDED
- [x] 8.3 Implement recovery via heartbeat — when heartbeat succeeds while suspended, reset backoff, set state to ACTIVE, start fresh full sync
- [x] 8.4 Implement bounded incremental ring buffer (64 entries) — enqueue from handler hooks, dequeue on timer tick
- [x] 8.5 Implement ring buffer overflow — drop oldest entry on full, emit debug log
- [x] 8.6 Implement DNS caching — resolve Mnemosyne hostname once on startup, store IP, refresh on SIGHUP and recovery
- [x] 8.7 Set HTTP connect timeout to 2 seconds, total request timeout to 5 seconds
- [x] 8.8 Implement tiered logging — first failure at WARN, one log per backoff level, one log on suspend, one on recovery, suppress heartbeat failures during suspension
- [x] 8.9 Implement stale cursor detection — discard persisted sync cursor if older than 1 hour on startup
- [x] 8.10 Ensure JSON buffers are freed after each POST (success or failure)

## 9. Event Loop Integration

- [x] 9.1 Register heartbeat timer (300s) on startup when Mnemosyne is configured
- [x] 9.2 Register periodic check timer (900s) on startup when Mnemosyne is configured
- [x] 9.3 Register/deregister chunk tick timer (2s) when chunked sync starts/finishes
- [x] 9.4 Add timer event handlers in main event loop — dispatch to heartbeat, periodic_check, or do_sync_tick based on timer ID
- [x] 9.5 Integrate incremental queue drain into timer tick — send queued incrementals when not in active chunked sync
- [x] 9.6 Add deregister call to shutdown path in `main.c`
- [x] 9.7 Add SIGHUP handler — stop existing timers, re-resolve DNS, reload config, restart timers if still configured

## 10. Build & Test

- [x] 10.1 Add new source files to Makefile — `http_client.c` (if new), `mnemosyne_sync.c` to `MOBIUS_SRCS`
- [x] 10.2 Write unit test for JSON escaping utility
- [x] 10.3 Write unit test for JSON payload builders (heartbeat, file chunk, msgboard chunk, news chunk, incrementals)
- [x] 10.4 Write unit test for sync state machine transitions (start → tick → finalize → chain)
- [x] 10.5 Write unit test for backoff/suspension state machine (fail → backoff → suspend → heartbeat recover)
- [x] 10.6 Write unit test for incremental ring buffer (enqueue, dequeue, overflow, drain)
- [ ] 10.7 Write integration test against live Mnemosyne instance (if `msv_` key available)
- [ ] 10.8 Test SIGHUP reload of Mnemosyne config
- [ ] 10.9 Test with unreachable Mnemosyne — verify backoff, suspension, log output, recovery
- [x] 10.10 Verify clean build on macOS and Linux (Docker)
