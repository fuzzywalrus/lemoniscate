## ADDED Requirements

### Requirement: Chunked full sync with state machine
The server SHALL perform initial content synchronization to Mnemosyne using a chunked full sync protocol with a persistent state machine. Chunks are sent on 2-second timer ticks to avoid blocking the event loop.

#### Scenario: First sync after configuration
- **WHEN** Mnemosyne sync is enabled and no prior sync state exists
- **THEN** the server SHALL initiate a chunked full sync, serializing all server content (files, news, message board) in batches via HTTP POST to the Mnemosyne API

#### Scenario: Sync interrupted by crash
- **WHEN** the server crashes during a chunked full sync
- **THEN** on restart, the server SHALL resume the sync from the last persisted state rather than starting over

#### Scenario: Chunked sync pacing
- **WHEN** a chunked full sync is in progress
- **THEN** the server SHALL send one chunk per 2-second timer tick to avoid starving the event loop

### Requirement: Incremental sync via event hooks
The server SHALL maintain a ring buffer (64 entries) of content change events and sync them incrementally to Mnemosyne as they occur.

#### Scenario: File uploaded triggers incremental sync
- **WHEN** a user uploads a file and Mnemosyne sync is active
- **THEN** the server SHALL add the file metadata to the ring buffer and sync it to Mnemosyne on the next timer tick

#### Scenario: Ring buffer overflow
- **WHEN** the ring buffer reaches capacity (64 entries) before events can be synced
- **THEN** the server SHALL drop the oldest entries and continue, triggering a full resync at the next drift detection interval

### Requirement: Drift detection
The server SHALL perform periodic drift detection (every 15 minutes) to verify that Mnemosyne's index matches the server's current content.

#### Scenario: Drift detected
- **WHEN** the 15-minute drift check finds a mismatch between local content and Mnemosyne's index
- **THEN** the server SHALL initiate a chunked full resync to correct the drift

#### Scenario: No drift
- **WHEN** the 15-minute drift check finds content in sync
- **THEN** the server SHALL take no action and schedule the next check

### Requirement: Heartbeat mechanism
The server SHALL send heartbeat pings to Mnemosyne at 5-minute intervals to maintain registration and detect connectivity issues.

#### Scenario: Heartbeat success
- **WHEN** the 5-minute heartbeat timer fires and Mnemosyne responds successfully
- **THEN** the server SHALL log the heartbeat at debug level and continue normal operation

#### Scenario: Heartbeat failure triggers recovery
- **WHEN** the heartbeat fails after the sync subsystem was suspended due to prior failures
- **THEN** the server SHALL use the heartbeat success as a canary to resume sync operations

### Requirement: Exponential backoff and sync suspension
The server SHALL implement exponential backoff on repeated sync failures and suspend sync operations after a threshold of consecutive failures.

#### Scenario: Consecutive failures trigger backoff
- **WHEN** a Mnemosyne API call fails
- **THEN** the server SHALL increase the retry delay exponentially and log the failure

#### Scenario: Sync suspension
- **WHEN** consecutive failures exceed the suspension threshold
- **THEN** the server SHALL suspend all sync operations (except heartbeat) until a heartbeat succeeds

### Requirement: Mnemosyne configuration
The server SHALL accept Mnemosyne connection parameters (host, port, API path, API key) via the server configuration file. API key SHALL be sent as a query parameter, not a header.

#### Scenario: Valid Mnemosyne config
- **WHEN** the server config contains a `mnemosyne` section with `host`, `port`, `api_key`, and `enabled: true`
- **THEN** the server SHALL initialize the Mnemosyne sync subsystem at startup

#### Scenario: Missing Mnemosyne config
- **WHEN** the server config does not contain a `mnemosyne` section
- **THEN** the server SHALL start normally with Mnemosyne sync disabled

#### Scenario: SIGHUP reload
- **WHEN** the server receives SIGHUP and the Mnemosyne config has changed
- **THEN** the server SHALL reinitialize the sync subsystem with the new configuration

### Requirement: Network resilience
The server SHALL use aggressive timeouts (2s connect, 5s total), DNS caching, and tiered logging to handle Mnemosyne connectivity issues without impacting server performance or flooding logs.

#### Scenario: Mnemosyne unreachable
- **WHEN** the Mnemosyne host is unreachable
- **THEN** the server SHALL timeout after 2 seconds on connect, log at warning level (not error), and enter backoff

#### Scenario: Log spam prevention during outage
- **WHEN** Mnemosyne is unreachable for an extended period
- **THEN** the server SHALL reduce log verbosity for repeated failures (tiered logging) to avoid filling disk
