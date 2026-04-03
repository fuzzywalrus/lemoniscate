## ADDED Requirements

### Requirement: Periodic content sync to Mnemosyne instance
The server SHALL periodically sync its content (message board posts, threaded news articles, and file listings) to a configured Mnemosyne indexing instance via HTTP. The sync interval SHALL be configurable with a default of 15 minutes. Content SHALL be pushed via `PATCH /api/v1/operator/servers/{server_id}` with the operator API key in the `X-API-Key` header (exact endpoint subject to confirmation).

#### Scenario: Sync triggers on interval
- **WHEN** the configured sync interval elapses and Mnemosyne is configured with a URL, API key, and server ID
- **THEN** the server collects enabled content types and sends them as a JSON payload via HTTP to the Mnemosyne instance

#### Scenario: No Mnemosyne URL configured
- **WHEN** the server starts without a Mnemosyne URL in its configuration
- **THEN** no sync timer is registered and no outbound HTTP requests are made

#### Scenario: Sync includes server identification
- **WHEN** a sync request is sent to the Mnemosyne instance
- **THEN** the request includes the configured server ID slug, server name, description, and address

### Requirement: Configurable content type selection
The server SHALL allow operators to select which content types are synced to Mnemosyne. Options are: message board posts (`msgboard`), threaded news articles (`news`), and file listings (`files`). All types SHALL be enabled by default when Mnemosyne sync is active.

#### Scenario: All content types enabled
- **WHEN** the server syncs with all content types enabled
- **THEN** the JSON payload includes message board posts, news articles, and file listings

#### Scenario: Files only
- **WHEN** the operator configures Mnemosyne sync with only `files` enabled
- **THEN** the sync payload includes file listings but omits message board posts and news articles

#### Scenario: Subset of content types
- **WHEN** the operator configures a subset of content types (e.g., `news` and `msgboard`)
- **THEN** only the selected content types are included in the sync payload

### Requirement: Operator-configured server ID
The server SHALL use an operator-configured server ID slug for Mnemosyne identification. The server ID SHALL be a lowercase alphanumeric string with hyphens and underscores, matching `^[a-z0-9_-]{3,32}$`, and SHALL be set in the server's configuration file.

#### Scenario: Valid server ID configured
- **WHEN** the server starts with a valid `server_id` in the Mnemosyne configuration
- **THEN** it uses that ID for all Mnemosyne API requests

#### Scenario: Missing server ID
- **WHEN** Mnemosyne URL and API key are configured but `server_id` is missing
- **THEN** Mnemosyne sync is disabled and the server logs a warning that the server ID is required

#### Scenario: Invalid server ID format
- **WHEN** the configured `server_id` does not match `^[a-z0-9_-]{3,32}$`
- **THEN** Mnemosyne sync is disabled and the server logs an error describing the format requirement

### Requirement: Operator API key authentication
The server SHALL authenticate with the Mnemosyne instance using an operator API key. The API key SHALL be sent in the `X-API-Key` HTTP header on all requests to operator-scoped endpoints. The API key is obtained out-of-band through the Mnemosyne operator registration process.

#### Scenario: API key configured
- **WHEN** an API key is set in the Mnemosyne configuration
- **THEN** all HTTP requests to Mnemosyne operator endpoints include the `X-API-Key` header with the configured value

#### Scenario: Missing API key
- **WHEN** Mnemosyne URL is configured but `api_key` is missing
- **THEN** Mnemosyne sync is disabled and the server logs a warning that an API key is required

### Requirement: Health check before enabling sync
The server SHALL probe the Mnemosyne instance's health endpoint (`GET /api/v1/health`) on startup before enabling the sync timer. If the health check fails, sync SHALL be disabled with a warning log.

#### Scenario: Health check succeeds
- **WHEN** the Mnemosyne instance responds to the health check with status `200` and `"status": "ok"`
- **THEN** the server enables the sync timer and logs that Mnemosyne sync is active

#### Scenario: Health check fails
- **WHEN** the Mnemosyne instance is unreachable or returns a non-200 status
- **THEN** the server logs a warning and disables Mnemosyne sync (no timer registered)

#### Scenario: Health check on degraded instance
- **WHEN** the Mnemosyne instance responds with `503` and `"status": "degraded"`
- **THEN** the server logs a warning and disables Mnemosyne sync

### Requirement: Graceful degradation on sync failure
The server SHALL handle sync failures gracefully. If a sync request fails (network error, timeout, HTTP error), the server SHALL log the error and continue operating normally. The next sync attempt SHALL proceed on the regular interval.

#### Scenario: Sync request times out
- **WHEN** the HTTP request to Mnemosyne does not complete within the configured timeout
- **THEN** the server logs a timeout warning and waits for the next sync interval

#### Scenario: Mnemosyne returns error
- **WHEN** the Mnemosyne instance returns an HTTP error status (4xx or 5xx)
- **THEN** the server logs the error status and message, and waits for the next sync interval

#### Scenario: Operator account not approved
- **WHEN** the Mnemosyne instance returns `403` with "operator account pending approval"
- **THEN** the server logs a warning indicating the operator account needs approval and waits for the next sync interval

#### Scenario: Network unreachable during sync
- **WHEN** the Mnemosyne instance becomes unreachable after initial health check
- **THEN** the server logs a connection error and continues normal Hotline operation

### Requirement: Message board content serialization
The server SHALL serialize message board posts as JSON objects containing post ID, author nickname, full body text, and timestamp.

#### Scenario: Message board sync
- **WHEN** the server syncs message board content
- **THEN** each post is serialized as a JSON object with `post_id`, `nick`, `body`, and `timestamp` fields

### Requirement: Threaded news content serialization
The server SHALL serialize threaded news articles as JSON objects containing the category path, article ID, title, poster name, date, and full body text.

#### Scenario: News article sync
- **WHEN** the server syncs news content
- **THEN** each article is serialized with `path`, `article_id`, `title`, `poster`, `date`, and `body` fields

#### Scenario: News category hierarchy preserved
- **WHEN** articles exist in nested news categories
- **THEN** the serialized `path` field reflects the full category path (e.g., `/General/Announcements`)

### Requirement: File listing content serialization
The server SHALL serialize file listings as JSON objects containing the file path, name, size in bytes, Hotline type code, creator code, file comment, and a directory flag.

#### Scenario: File listing sync
- **WHEN** the server syncs file content
- **THEN** each file entry is serialized with `path`, `name`, `size`, `type`, `creator`, `comment`, and `is_dir` fields

#### Scenario: Directory entries
- **WHEN** a directory is encountered during file listing serialization
- **THEN** the entry has `is_dir` set to true and `size` set to 0
