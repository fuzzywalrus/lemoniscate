## ADDED Requirements

### Requirement: Outbound HTTP/1.1 client for Mnemosyne sync
The server SHALL include a minimal HTTP/1.1 client capable of sending GET, POST, and PATCH requests with JSON bodies to a Mnemosyne instance. The client SHALL use BSD sockets with configurable connect and read timeouts. All operator-scoped requests SHALL include the `X-API-Key` header.

#### Scenario: HTTP PATCH with JSON body
- **WHEN** the server sends a sync payload to Mnemosyne
- **THEN** it sends an HTTP/1.1 PATCH request with `Content-Type: application/json` and `X-API-Key` headers and the JSON body

#### Scenario: HTTP GET for health check
- **WHEN** the server probes the Mnemosyne health endpoint
- **THEN** it sends an HTTP/1.1 GET request and parses the HTTP status code from the response

#### Scenario: Connect timeout
- **WHEN** the TCP connection to the Mnemosyne host cannot be established within 5 seconds
- **THEN** the HTTP client returns a timeout error

#### Scenario: Read timeout
- **WHEN** the Mnemosyne instance does not respond within 10 seconds after connection
- **THEN** the HTTP client returns a timeout error

### Requirement: Sync timer in kqueue event loop
The server SHALL register a kqueue `EVFILT_TIMER` event for the Mnemosyne sync interval. The timer fires periodically and triggers a sync cycle in the main event loop.

#### Scenario: Timer registration
- **WHEN** Mnemosyne sync is enabled and the health check succeeds
- **THEN** a kqueue timer is registered with the configured sync interval

#### Scenario: Timer fires
- **WHEN** the kqueue timer event fires
- **THEN** the server executes a full sync cycle (collect content, serialize, PATCH to Mnemosyne)

#### Scenario: Timer removed on disable
- **WHEN** Mnemosyne sync is disabled (via SIGHUP or health check failure)
- **THEN** the kqueue timer is removed from the event loop
