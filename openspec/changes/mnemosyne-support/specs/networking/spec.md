## ADDED Requirements

### Requirement: Outbound HTTP POST for Mnemosyne sync
The server SHALL use the HTTP client to send POST requests with JSON bodies to the Mnemosyne sync API. The API key SHALL be appended as a `?api_key=` query parameter. The `Content-Type` header SHALL be `application/json`.

#### Scenario: POST with JSON body
- **WHEN** the server sends a sync payload to Mnemosyne
- **THEN** it sends an HTTP/1.1 POST with `Content-Type: application/json` and `?api_key=msv_...` in the URL

#### Scenario: Connect timeout
- **WHEN** the TCP connection to the Mnemosyne host cannot be established within 5 seconds
- **THEN** the HTTP client returns a timeout error and the sync chunk is skipped

#### Scenario: Read timeout
- **WHEN** the Mnemosyne instance does not respond within 10 seconds
- **THEN** the HTTP client returns a timeout error and the sync chunk is skipped

### Requirement: Multiple sync timers in event loop
The server SHALL register up to three timers for Mnemosyne sync: a heartbeat timer (every 300 seconds), a periodic drift check timer (every 900 seconds), and a chunk tick timer (every 2 seconds, only active during a chunked sync).

#### Scenario: Heartbeat timer
- **WHEN** Mnemosyne is configured and the server starts
- **THEN** a 300-second repeating timer is registered for heartbeat POSTs

#### Scenario: Periodic check timer
- **WHEN** Mnemosyne is configured and the server starts
- **THEN** a 900-second repeating timer is registered for drift detection

#### Scenario: Chunk tick timer
- **WHEN** a chunked sync begins
- **THEN** a 2-second repeating timer is registered to drive the sync state machine

#### Scenario: Chunk tick timer removed
- **WHEN** a chunked sync completes (finalize sent) or is aborted
- **THEN** the 2-second chunk tick timer is removed
