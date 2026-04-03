## Context

Lemoniscate's Mnemosyne sync module (`mnemosyne_sync.c`) implements the full sync protocol: heartbeat, chunked file/news sync, incremental updates, backoff/suspension, and deregistration. Unit tests in `test_mnemosyne.c` cover the JSON builders, URL parsing, ring buffer, and state checks — but nothing verifies the protocol works against a real Mnemosyne instance.

The live instance is `tracker.vespernet.net:8980`. Operators register at `agora.vespernet.net` to get `msv_`-prefixed server API keys. We've confirmed HTTP connectivity (got proper 401 for invalid keys).

The existing test infrastructure uses simple assert-based C tests with no external framework. Integration tests will follow the same pattern but require network access and a valid API key.

## Goals / Non-Goals

**Goals:**
- Verify the full sync lifecycle against `tracker.vespernet.net:8980` with a real API key
- Confirm synced content is searchable via Mnemosyne's `/api/v1/search` endpoint
- Test error paths: invalid key (401), unreachable host (backoff/suspension)
- Keep integration tests separate from unit tests so CI can run without a live key
- Test SIGHUP-triggered reconfiguration of the sync module

**Non-Goals:**
- Testing the Mnemosyne server itself (that's VesperNet's responsibility)
- Performance/load testing of the sync protocol
- Testing Hotline Navigator's search UI
- Replacing the existing unit tests — integration tests supplement them

## Decisions

### 1. Environment variable for API key (`MSV_API_KEY`)

**Decision:** Read the `msv_` key from the `MSV_API_KEY` environment variable. If not set, print "SKIPPED (no MSV_API_KEY)" for each integration test and exit 0.

**Rationale:** This keeps credentials out of the codebase and lets CI pass without a key. Developers with a key run `MSV_API_KEY=msv_... make test-mnemosyne-live`.

**Alternatives considered:**
- Config file: More moving parts, risk of accidental commit.
- Command-line argument: Awkward with `make` targets.

### 2. Separate test binary and Makefile target

**Decision:** Integration tests go in `test/test_mnemosyne_live.c`, built as `test_mnemosyne_live`, invoked via `make test-mnemosyne-live`. Not included in `make test`.

**Rationale:** `make test` must pass without network access. Integration tests hit a real server and may take seconds per test (HTTP round-trips). Keeping them separate avoids CI breakage and slow test suites.

### 3. Test harness builds a minimal mn_sync_t without the event loop

**Decision:** Integration tests construct an `mn_sync_t` directly, populate it with config/key, resolve DNS, and call the sync functions (`mn_send_heartbeat`, `mn_start_full_sync`, `mn_do_sync_tick`, etc.) in a manual loop — no event loop or timers.

**Rationale:** The event loop integration is tested implicitly by running the server. The integration tests focus on verifying the HTTP protocol and JSON payloads are accepted by Mnemosyne. Driving the state machine manually makes tests deterministic and fast.

### 4. Verify search results via `/api/v1/search`

**Decision:** After syncing test content, query `/api/v1/search?q=<unique_marker>` and assert the content appears. Use a unique marker string (e.g., timestamp-based) in test file names and news titles to avoid collisions with other servers' content.

**Rationale:** This closes the loop — we're not just pushing data, we're confirming it's indexed and searchable. The search API is the client-facing endpoint that Hotline Navigator uses.

### 5. Clean up after tests via deregister

**Decision:** Each test run calls `mn_deregister()` at teardown to remove the test server from Mnemosyne's index.

**Rationale:** Prevents test data from polluting the live index. Deregister is part of the protocol and should be tested anyway.

## Risks / Trade-offs

- **[Live service dependency]** — Tests fail if `tracker.vespernet.net` is down. Mitigation: tests skip gracefully on connection failure; they're not in the default `make test` target.

- **[Search indexing latency]** — Synced content may not appear in search results immediately. Mitigation: add a short delay (1-2 seconds) between sync and search verification; retry once if not found.

- **[Test data in production index]** — Test files/news briefly appear in the live index. Mitigation: use obvious test markers (`__lemoniscate_test_`), deregister on cleanup, and keep test content minimal.

- **[API key management]** — Developers need a valid key to run integration tests. Mitigation: documented in the test file header and Makefile; registration URL is `agora.vespernet.net`.
