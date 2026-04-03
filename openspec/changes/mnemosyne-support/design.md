## Context

Lemoniscate is a C-based Hotline server using a kqueue event loop, libyaml for configuration, and a handler dispatch table for transaction processing. It currently only accepts inbound TCP connections (Hotline protocol + file transfers) and sends outbound UDP packets for tracker registration.

Mnemosyne is an external indexing service that aggregates Hotline server content (message board posts, news articles, file listings) and exposes a REST API for search. Servers opt in by registering as an operator, getting approved, then periodically syncing content.

**Discovered API model (from live probing of `tracker.vespernet.net:8980`):**

The Mnemosyne server-side API uses an operator/server hierarchy:

1. **Operator registration:** `POST /api/v1/register` with `{id, name, address}` returns an API key (`mop_`-prefixed) and a pending-approval status. Admin must approve the operator before sync works.
2. **Server management:** Once approved, `POST /api/v1/operator/servers` registers a server, `PATCH /api/v1/operator/servers/{server_id}` updates it. All operator endpoints require `X-API-Key` header.
3. **Server IDs are operator-chosen slugs** (e.g., `vnet`, `hotline-central-hub`), not UUIDs. Must match `^[a-z0-9_-]{3,32}$`.
4. **Heartbeat is separate from sync** — live data shows distinct `last_heartbeat` and `last_sync` timestamps. Heartbeat mechanism is unknown (may be a lightweight GET or embedded in the sync).
5. **Content push endpoint** — likely `PATCH /api/v1/operator/servers/{id}` with content arrays in the body. Exact payload format awaiting confirmation from fogWraith.

The server has no HTTP client capability today. All content (flat news, threaded news, file listings) is accessible via existing internal APIs (`mobius_flat_news_t`, `mobius_threaded_news_t`, `hl_file_store_t`).

## Goals / Non-Goals

**Goals:**
- Periodically sync server content to a Mnemosyne instance over HTTP
- Support configurable Mnemosyne URL, operator API key, server ID slug, sync interval, and content types
- Integrate with the existing kqueue event loop using a timer (no threads for sync scheduling)
- Gracefully handle Mnemosyne unavailability without affecting Hotline server operation
- Authenticate with Mnemosyne using the operator API key (`X-API-Key` header)

**Non-Goals:**
- Implementing the Mnemosyne indexing service itself
- Automating operator registration/approval (this is a one-time out-of-band step)
- Supporting HTTPS for the sync connection (HTTP only for initial implementation)
- Real-time push on content changes (periodic batch sync only)
- Implementing the client-side Mnemosyne search API (that's a Hotline client concern)
- Thread-safe concurrent sync (sync runs in the main event loop)

## Decisions

### 1. Minimal HTTP/1.1 client using BSD sockets

**Decision:** Implement a minimal HTTP/1.1 client using raw BSD sockets rather than adding a dependency like libcurl. Must support GET, POST, and PATCH methods.

**Rationale:** The project targets Mac OS X 10.4 Tiger (PowerPC) on the main branch. libcurl may not be available or may require a specific build. The sync only needs HTTP GET/POST/PATCH with JSON bodies and basic response status parsing. A minimal implementation (~300 lines) keeps the dependency footprint zero and matches the project's existing socket-based networking style.

**Alternatives considered:**
- libcurl: Full-featured but adds a dependency that complicates the Tiger build.
- NSURLConnection (Obj-C): Only available in the GUI target, not the CLI server.

### 2. JSON serialization via string formatting

**Decision:** Build JSON payloads using snprintf/string formatting rather than adding a JSON library.

**Rationale:** The sync payloads are structured JSON objects with known shapes. The server already uses snprintf extensively. A JSON library (cJSON, jansson) adds a dependency for minimal benefit. Response parsing only needs HTTP status codes and simple field extraction.

**Alternatives considered:**
- cJSON: Lightweight but still an external dependency. Would be warranted if we needed complex JSON response parsing.

### 3. Timer-based sync in the kqueue event loop

**Decision:** Use `EVFILT_TIMER` in the existing kqueue event loop to trigger periodic sync, with non-blocking socket I/O for the HTTP request.

**Rationale:** This matches the server's existing architecture (kqueue for everything). The sync runs in the main event loop, avoiding thread synchronization issues. If the HTTP request blocks (DNS resolution, slow Mnemosyne), it will briefly stall the event loop — acceptable for a background sync that runs every 15 minutes by default.

**Alternatives considered:**
- Separate pthread for sync: Adds threading complexity and requires locking around content access (flat news, threaded news, file store). Not worth it for a periodic background task.
- SIGALRM timer: Less precise and harder to manage alongside kqueue.

### 4. Operator-chosen server ID slug (not generated UUID)

**Decision:** The server ID is a human-readable slug configured by the operator in config.yaml, not a generated UUID.

**Rationale:** Live API probing confirmed that Mnemosyne server IDs are operator-chosen strings matching `^[a-z0-9_-]{3,32}$` (e.g., `vnet`, `hotline-central-hub`). This is a configuration value, not something the server generates.

**Alternatives considered (previously):**
- UUID v4 generation: This was the original design assumption, but live data showed slug-based IDs. Dropped.

### 5. Content sync as full snapshots

**Decision:** Each sync cycle sends a full snapshot of enabled content types, not incremental diffs.

**Rationale:** The content volume of a typical Hotline server (hundreds of posts/files, not millions) makes full snapshots feasible. Full snapshots are simpler to implement and reason about. The exact API may support incremental updates, but snapshots work as a starting point.

**Alternatives considered:**
- Incremental sync with change tracking: Requires tracking modification timestamps across all content types. Can be added later if needed.

### 6. Out-of-band operator registration

**Decision:** Operator registration and approval happen outside the server software. The operator registers via curl/browser, gets approved by the Mnemosyne admin, then enters the API key and server ID in config.yaml.

**Rationale:** Registration is a one-time setup step requiring admin approval. Automating it in the server binary adds complexity for no recurring benefit. The server only needs the resulting API key and server ID.

## Risks / Trade-offs

- **[Undocumented push API]** → The exact content push payload format is not publicly documented. Our best hypothesis is `PATCH /api/v1/operator/servers/{id}` with content arrays. Mitigation: awaiting confirmation from fogWraith. The design is structured so the sync payload builder is isolated and easy to adjust once the API is confirmed.

- **[Blocking event loop during sync]** → The HTTP request runs in the main kqueue loop. DNS resolution and slow Mnemosyne responses could stall transaction processing for seconds. Mitigation: set aggressive socket timeouts (5 seconds connect, 10 seconds total). For a 15-minute sync interval, brief stalls are acceptable.

- **[No HTTPS support]** → Sync data including the API key is sent in plaintext. Mitigation: Mnemosyne content is public anyway (it's an indexing service for discovery). The API key only authorizes content push, not destructive operations. HTTPS can be added later if needed.

- **[Large file listings]** → Servers with thousands of files could produce large JSON payloads. Mitigation: file sync only includes metadata (name, path, size, type, comment), not file contents. Even 10,000 files produce a manageable payload (~2-5 MB).

- **[Operator approval delay]** → New operators must wait for manual approval before sync works. Mitigation: this is a Mnemosyne design choice, not something we can change. Document it clearly so operators know to register ahead of time.

## Open Questions

- **Exact content push endpoint and payload** — Is it `PATCH /api/v1/operator/servers/{id}` with content arrays? Or a separate content-specific endpoint? Awaiting fogWraith's response.
- **Heartbeat mechanism** — Live data shows `last_heartbeat` separate from `last_sync`. Is heartbeat a lightweight ping, or does it piggyback on the sync request?
- **Server creation** — Does `POST /api/v1/operator/servers` auto-create the server from the first sync, or must it be explicitly created with metadata before content can be pushed?
