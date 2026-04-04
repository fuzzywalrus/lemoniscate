## ADDED Requirements

### Requirement: Graceful skip when no API key is available
Integration tests SHALL read the `MSV_API_KEY` environment variable. When the variable is not set or empty, all integration tests SHALL print "SKIPPED" and exit with code 0.

#### Scenario: No API key set
- **WHEN** `MSV_API_KEY` is not set and integration tests are invoked
- **THEN** each test prints "SKIPPED (no MSV_API_KEY)" and the process exits 0

#### Scenario: API key provided
- **WHEN** `MSV_API_KEY` is set to a valid `msv_`-prefixed key
- **THEN** all integration tests execute against the configured Mnemosyne instance

### Requirement: Heartbeat integration test
The test suite SHALL verify that a heartbeat POST to `/api/v1/sync/heartbeat` succeeds with a valid API key and returns HTTP 200.

#### Scenario: Successful heartbeat
- **WHEN** `mn_send_heartbeat` is called with a valid API key and server metadata
- **THEN** the Mnemosyne instance returns HTTP 200

#### Scenario: Heartbeat with invalid key
- **WHEN** a heartbeat is sent with an invalid API key
- **THEN** the Mnemosyne instance returns HTTP 401

### Requirement: Chunked file sync integration test
The test suite SHALL verify that a full chunked file sync completes successfully. The test SHALL create a temporary file directory with known test files, run the file sync state machine to completion, and verify all chunks are accepted.

#### Scenario: File sync with test files
- **WHEN** a file sync runs against a directory containing test files with unique marker names
- **THEN** all chunk POSTs return HTTP 200 and the finalize chunk is accepted

#### Scenario: File sync with empty directory
- **WHEN** a file sync runs against an empty directory
- **THEN** only the finalize chunk is sent and accepted

### Requirement: Chunked news sync integration test
The test suite SHALL verify that a full chunked news sync completes successfully. The test SHALL use a threaded news structure with test categories and articles.

#### Scenario: News sync with test articles
- **WHEN** a news sync runs with test categories and articles containing unique markers
- **THEN** all chunk POSTs (categories, articles, finalize) return HTTP 200

### Requirement: Incremental sync integration test
The test suite SHALL verify that incremental file add, file remove, and news add POSTs are accepted by the Mnemosyne instance.

#### Scenario: Incremental file add
- **WHEN** an incremental file add POST is sent with a test file entry
- **THEN** Mnemosyne returns HTTP 200

#### Scenario: Incremental file remove
- **WHEN** an incremental file remove POST is sent with a test file path
- **THEN** Mnemosyne returns HTTP 200

#### Scenario: Incremental news add
- **WHEN** an incremental news add POST is sent with a test article
- **THEN** Mnemosyne returns HTTP 200

### Requirement: Search verification after sync
The test suite SHALL verify that content synced to Mnemosyne is searchable via the `/api/v1/search` endpoint. A unique test marker SHALL be used in content to avoid collisions.

#### Scenario: Synced files appear in search
- **WHEN** files with unique marker names are synced and then searched via `/api/v1/search?q=<marker>&type=files`
- **THEN** the search results contain at least one match with the test server name

#### Scenario: Synced news appears in search
- **WHEN** news articles with unique marker titles are synced and then searched via `/api/v1/search?q=<marker>&type=news`
- **THEN** the search results contain at least one match with the test server name

### Requirement: Deregistration integration test
The test suite SHALL verify that deregistration removes the test server from the Mnemosyne index. Deregistration SHALL also be called during test teardown to clean up.

#### Scenario: Deregister after sync
- **WHEN** `mn_deregister` is called after a successful sync
- **THEN** Mnemosyne returns HTTP 200 and the test server no longer appears in search results

### Requirement: Error handling integration tests
The test suite SHALL verify error handling for invalid keys and unreachable hosts.

#### Scenario: Invalid API key rejected
- **WHEN** a sync request is sent with an invalid `msv_` key
- **THEN** the HTTP response status is 401

#### Scenario: Unreachable host triggers backoff
- **WHEN** sync is attempted against a non-routable host (e.g., `192.0.2.1:9999`)
- **THEN** `mn_send_heartbeat` returns -1 and the consecutive failure counter increments

### Requirement: SIGHUP reconfiguration test
The test suite SHALL verify that `mn_sync_reconfigure` correctly updates the sync manager when config changes.

#### Scenario: URL change triggers re-resolve
- **WHEN** `mn_sync_reconfigure` is called with a changed Mnemosyne URL
- **THEN** DNS is re-resolved and the new URL is used for subsequent requests

#### Scenario: Config removal disables sync
- **WHEN** `mn_sync_reconfigure` is called with an empty URL
- **THEN** `mn_sync_enabled` returns 0

### Requirement: Separate Makefile target
Integration tests SHALL be built and run via `make test-mnemosyne-live`. This target SHALL NOT be included in the default `make test` target.

#### Scenario: make test does not run integration tests
- **WHEN** `make test` is invoked without `MSV_API_KEY`
- **THEN** integration tests are not executed

#### Scenario: make test-mnemosyne-live runs integration tests
- **WHEN** `make test-mnemosyne-live` is invoked
- **THEN** the integration test binary is built and executed
