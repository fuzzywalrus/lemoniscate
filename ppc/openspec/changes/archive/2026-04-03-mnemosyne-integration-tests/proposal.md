## Why

The Mnemosyne sync implementation has unit tests for individual components (JSON builders, URL parsing, ring buffer) but no integration tests that verify the full sync protocol against a live Mnemosyne instance. With access to a valid `msv_` API key from agora.vespernet.net, we can verify that heartbeats, chunked file/news sync, incremental updates, and deregistration all work end-to-end against `tracker.vespernet.net:8980`.

## What Changes

- Add an integration test suite that runs the full Mnemosyne sync lifecycle against a live instance when an `msv_` API key is available (skipped gracefully when no key is configured).
- Add a test harness that can stand up a minimal server context (config, file root with test files, threaded news with test articles) without running the full event loop.
- Add verification that synced content appears in Mnemosyne's search API (`/api/v1/search`).
- Add tests for error handling: invalid key rejection (HTTP 401), unreachable host backoff/suspension, SIGHUP reconfiguration.
- Add a `make test-mnemosyne-live` target that runs integration tests (separate from `make test` so CI doesn't require a live key).

## Capabilities

### New Capabilities
- `mnemosyne-integration-testing`: Integration test suite for verifying the Mnemosyne sync protocol against a live instance. Covers heartbeat, chunked sync, incremental sync, search verification, deregistration, error handling, and SIGHUP reload.

### Modified Capabilities

## Impact

- **New files**: `test/test_mnemosyne_live.c` (integration tests), updates to `Makefile` (new test target)
- **Configuration**: Tests read `MSV_API_KEY` environment variable; skip gracefully if not set
- **Network**: Tests make real HTTP requests to `tracker.vespernet.net:8980` — requires network access
- **Existing tests**: No changes to existing `test_mnemosyne.c` unit tests
