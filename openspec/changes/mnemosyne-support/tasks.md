## 1. Configuration

- [ ] 1.1 Add Mnemosyne config fields to `hl_config_t` in `include/hotline/config.h` — URL string, API key string, server ID string, sync interval int, content type flags bitmask
- [ ] 1.2 Add `mnemosyne` section parsing to `config_loader.c` — parse `url`, `api_key`, `server_id`, `sync_interval`, and `content_types` from YAML
- [ ] 1.3 Add server ID format validation (`^[a-z0-9_-]{3,32}$`) — reject and log error if invalid
- [ ] 1.4 Add required field validation — disable sync and warn if `url` present but `api_key` or `server_id` missing
- [ ] 1.5 Add SIGHUP reload support for Mnemosyne config fields (update URL, key, server ID, interval, content types on reload)

## 2. HTTP Client

- [ ] 2.1 Create `include/hotline/http_client.h` with types for HTTP request/response and function declarations
- [ ] 2.2 Implement minimal HTTP/1.1 client in `src/hotline/http_client.c` — TCP connect with 5s timeout, send request, read response status and body
- [ ] 2.3 Implement HTTP GET method (for health check endpoint)
- [ ] 2.4 Implement HTTP POST method with `Content-Type: application/json` and optional `X-API-Key` header
- [ ] 2.5 Implement HTTP PATCH method (same headers as POST, for content sync)
- [ ] 2.6 Implement URL parsing (extract host, port, path from Mnemosyne URL string)
- [ ] 2.7 Implement 10-second read timeout using `setsockopt` `SO_RCVTIMEO`

## 3. Content Serialization

- [ ] 3.1 Create `include/mobius/mnemosyne_sync.h` with sync function declarations and content type flags
- [ ] 3.2 Implement message board serialization — iterate `mobius_flat_news_t` posts, format as JSON array with `post_id`, `nick`, `body`, `timestamp`
- [ ] 3.3 Implement threaded news serialization — walk `mobius_threaded_news_t` categories and articles, format as JSON array with `path`, `article_id`, `title`, `poster`, `date`, `body`
- [ ] 3.4 Implement file listing serialization — recursively walk `hl_file_store_t`, format as JSON array with `path`, `name`, `size`, `type`, `creator`, `comment`, `is_dir`
- [ ] 3.5 Implement full sync payload builder — combine server metadata (server_id, name, description, address) with enabled content type arrays into a single JSON object
- [ ] 3.6 Add JSON string escaping utility for special characters in content fields (quotes, backslashes, newlines, control characters)

## 4. Sync Engine

- [ ] 4.1 Implement health check probe — GET `/api/v1/health`, parse HTTP status, log result, return success/failure
- [ ] 4.2 Implement sync cycle function — build payload, PATCH to `/api/v1/operator/servers/{server_id}` (endpoint subject to confirmation), include `X-API-Key` header, handle response status, log errors
- [ ] 4.3 Handle 403 "operator account pending approval" response — log specific warning message
- [ ] 4.4 Register `EVFILT_TIMER` in kqueue event loop for sync interval when health check succeeds
- [ ] 4.5 Add kqueue event handler for the sync timer — call sync cycle on timer fire
- [ ] 4.6 Implement timer removal on sync disable (SIGHUP removes Mnemosyne config, or health check fails)
- [ ] 4.7 Add startup integration in `main.c` — after server init, validate config, probe health, register timer if enabled

## 5. Build & Test

- [ ] 5.1 Add new source files to `Makefile` — `http_client.c` to `HOTLINE_C_SRCS`; `mnemosyne_sync.c` to `MOBIUS_SRCS`
- [ ] 5.2 Write unit test for server ID validation (valid slugs, invalid formats, boundary lengths)
- [ ] 5.3 Write unit test for HTTP request formatting (verify headers, methods, body, URL parsing)
- [ ] 5.4 Write unit test for JSON serialization (verify escaping, structure, content type filtering)
- [ ] 5.5 Write integration test with a mock HTTP server (verify health check, sync cycle, error handling, 403 response)
- [ ] 5.6 Test SIGHUP reload of Mnemosyne config (enable, disable, change URL)
- [ ] 5.7 Verify clean build on both modern (macOS 10.11+) and Tiger (10.4 PPC) targets

## 6. Pending (awaiting fogWraith confirmation)

- [ ] 6.1 Confirm exact content push endpoint and HTTP method (`PATCH /api/v1/operator/servers/{id}` or other)
- [ ] 6.2 Confirm content push payload format (content arrays in body, separate fields, etc.)
- [ ] 6.3 Confirm heartbeat mechanism (separate endpoint, piggyback on sync, or automatic)
- [ ] 6.4 Confirm server creation flow (auto-create on first sync vs explicit `POST /api/v1/operator/servers`)
- [ ] 6.5 Update sync engine implementation once API is confirmed
