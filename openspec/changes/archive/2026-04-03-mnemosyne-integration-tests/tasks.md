## 1. Test Harness Setup

- [ ] 1.1 Create `test/test_mnemosyne_live.c` with `MSV_API_KEY` env var check — skip all tests with exit 0 if not set
- [ ] 1.2 Add test helper to build a minimal `mn_sync_t` from env var key + `tracker.vespernet.net:8980` (DNS resolve, URL parse, no event loop)
- [ ] 1.3 Add test helper to create a temp directory with uniquely-named test files (use `__lemoniscate_test_<timestamp>` prefix)
- [ ] 1.4 Add test helper to build a minimal `mobius_threaded_news_t` with a test category and articles using unique markers
- [ ] 1.5 Add test cleanup function that calls `mn_deregister` and removes temp files
- [ ] 1.6 Add `make test-mnemosyne-live` target to Makefile (not included in `make test`)

## 2. Heartbeat Tests

- [ ] 2.1 Test successful heartbeat — `mn_send_heartbeat` returns 0 with valid key, verify HTTP 200
- [ ] 2.2 Test heartbeat with invalid key — construct `mn_sync_t` with `msv_invalid`, verify returns -1

## 3. Chunked Full Sync Tests

- [ ] 3.1 Test file sync with test directory — run `mn_start_full_sync` + `mn_do_sync_tick` loop until `chunked_sync_active` is 0, verify no suspension
- [ ] 3.2 Test file sync with empty directory — verify finalize-only chunk completes without error
- [ ] 3.3 Test news sync with test articles — verify categories and articles chunks all succeed
- [ ] 3.4 Test full sync chaining — verify files sync runs first, then news, then `chunked_sync_active` clears

## 4. Incremental Sync Tests

- [ ] 4.1 Test incremental file add — queue a test file via `mn_queue_file_add`, drain with `mn_drain_incremental_queue`, verify no failure
- [ ] 4.2 Test incremental file remove — queue a remove via `mn_queue_file_remove`, drain, verify no failure
- [ ] 4.3 Test incremental news add — queue a test article via `mn_queue_news_add`, drain, verify no failure

## 5. Search Verification

- [ ] 5.1 Add HTTP GET helper for querying `/api/v1/search` (reuse `hl_http_post` or add a simple GET)
- [ ] 5.2 After full file sync, search for unique file marker — verify at least one result matches test server
- [ ] 5.3 After full news sync, search for unique article marker — verify at least one result matches test server
- [ ] 5.4 After deregister, search for marker — verify results no longer include test server

## 6. Error Handling Tests

- [ ] 6.1 Test invalid API key — heartbeat with bad key returns -1, status 401
- [ ] 6.2 Test unreachable host — heartbeat against `192.0.2.1:9999` returns -1, consecutive failures increment
- [ ] 6.3 Test backoff state — after 5 failures against unreachable host, verify `state == MN_STATE_SUSPENDED`

## 7. SIGHUP Reconfiguration Tests

- [ ] 7.1 Test `mn_sync_reconfigure` with changed URL — verify DNS re-resolves to new address
- [ ] 7.2 Test `mn_sync_reconfigure` with empty URL — verify `mn_sync_enabled` returns 0
- [ ] 7.3 Test `mn_sync_reconfigure` with changed API key — verify new key is used for subsequent requests

## 8. Build & Verify

- [ ] 8.1 Verify `make test-mnemosyne-live` builds and skips cleanly without `MSV_API_KEY`
- [ ] 8.2 Verify `make test` does not invoke integration tests
- [ ] 8.3 Run full integration suite with valid `MSV_API_KEY` — all tests pass
- [ ] 8.4 Verify clean build on macOS and Linux (Docker)
