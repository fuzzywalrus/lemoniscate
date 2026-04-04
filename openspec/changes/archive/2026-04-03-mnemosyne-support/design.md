## Context

Lemoniscate is a C-based Hotline server using a platform-abstracted event loop (kqueue on macOS, epoll on Linux), libyaml for configuration, and a handler dispatch table for transaction processing. It already has an HTTP client capability from the linux-platform-support work (or will build a minimal one using BSD sockets).

Mnemosyne is an external indexing service. The server-side sync API was confirmed via fogWraith's reference Janus plugin (`mnemosyne.lua`). The API uses:

- **Authentication:** `?api_key=msv_...` query parameter on all POST requests
- **Heartbeat:** `POST /api/v1/sync/heartbeat` — server name, description, content counts
- **Chunked full sync:** `POST /api/v1/sync/{type}/chunked` — sync_id-based chunked protocol with finalize
- **Incremental sync:** `POST /api/v1/sync/{type}` — event-driven adds/removes
- **Deregistration:** `POST /api/v1/sync/deregister` — called on server shutdown

Content is synced via three separate endpoint families: `/sync/files/...`, `/sync/msgboard/...`, `/sync/news/...`.

All content (flat news, threaded news, file listings) is accessible via existing internal APIs (`mobius_flat_news_t`, `mobius_threaded_news_t`, `hl_file_store_t`).

## Goals / Non-Goals

**Goals:**
- Implement the full Mnemosyne sync protocol: heartbeat, chunked full sync, incremental sync, drift detection, deregistration
- Support configurable Mnemosyne URL, `msv_`-prefixed server API key, and per-content-type indexing toggles
- Integrate with the platform event loop using timers (heartbeat, periodic check, chunk tick)
- Persist sync state so interrupted syncs can resume after restart
- Push incremental changes in response to content events (post, upload, delete)
- Gracefully handle Mnemosyne unavailability without affecting Hotline server operation

**Non-Goals:**
- Implementing the Mnemosyne indexing service itself
- Supporting HTTPS for the sync connection (HTTP only for initial implementation)
- Implementing the client-side Mnemosyne search API
- Thread-safe concurrent sync (sync runs in the main event loop)
- Operator registration flow (one-time out-of-band step)
- Message board sync — the flat message board (`MessageBoard.txt`) stores posts as raw unstructured text (prepend-only, no post IDs, no pagination, no deletion). The Mnemosyne API expects structured posts with `{id, nick, body, timestamp}`. Syncing it requires a text parser to extract posts from the raw format, which is a separate change. Files and news are fully supported.

## Decisions

### 1. Port sync logic to C (not run the Lua plugin)

**Decision:** Implement the Mnemosyne sync protocol directly in C, not by embedding a Lua runtime to run the reference plugin.

**Rationale:** Lemoniscate has no Lua runtime and adding one (Lua 5.1 + the `server.*` API bindings) would be a much larger project than porting ~500 lines of state machine logic to C. We have direct access to all the content managers that the Lua plugin accesses through the `server.*` API.

**Alternatives considered:**
- Embed Lua 5.1: Adds a runtime dependency, requires binding all `server.*` APIs. Overkill when the sync logic is straightforward.

### 2. Chunked sync with state machine (matching reference plugin)

**Decision:** Implement chunked sync exactly as the reference plugin does — one content type at a time, one chunk per 2-second timer tick, with a sync_id for correlation and a finalize flag on the last chunk.

**Rationale:** This is the protocol that the Mnemosyne server expects. Deviating (e.g., sending everything in one POST) would likely break the server-side processing. The chunked approach also avoids large payloads and spreads I/O over time.

### 3. API key via query parameter (not header)

**Decision:** Send the API key as `?api_key=msv_...` on every POST URL, not via the `X-API-Key` header.

**Rationale:** The reference plugin uses query parameter authentication. The Mnemosyne server may not check the header for sync endpoints. Follow the reference implementation exactly.

### 4. Sync state persistence for crash recovery

**Decision:** Persist the sync cursor (content type, sync_id, chunk_index, position) to a file in the config directory. On startup, check for an interrupted sync and resume it.

**Rationale:** The reference plugin uses `server.save()` for this. A chunked file sync of a large server could take minutes — losing progress on crash means re-scanning thousands of files. Persistence is cheap (one small file write per chunk).

### 5. JSON serialization via string formatting

**Decision:** Build JSON payloads using snprintf/dynamic buffer rather than a JSON library.

**Rationale:** The sync payloads are structured JSON with known shapes (arrays of objects with fixed fields). A JSON string escaping utility handles special characters. This avoids adding cJSON or jansson as a dependency.

### 6. Incremental sync via transaction handler hooks

**Decision:** Hook into the existing transaction handler responses to detect content changes and trigger incremental sync POSTs. Specifically: after message board post/delete, after file upload/delete/move, after news article post.

**Rationale:** The reference plugin uses Janus event hooks (`on_msgboard_post`, `on_upload`, `on_delete_file`, etc.). In Lemoniscate, we can trigger incremental syncs from the transaction handler return path — when a handler successfully modifies content, fire the corresponding incremental sync.

### 7. Three timers: heartbeat (5 min), periodic check (15 min), chunk tick (2 sec)

**Decision:** Register three event loop timers matching the reference plugin's intervals.

**Rationale:** The heartbeat keeps the server "alive" in Mnemosyne's view. The periodic check detects content drift (e.g., files changed outside Hotline). The chunk tick drives the chunked sync state machine — it's only active during a sync, sending one chunk per tick to avoid overwhelming Mnemosyne.

### 8. Drop box directory filtering

**Decision:** Skip directories with drop box access during file sync, matching the reference plugin behavior.

**Rationale:** Drop boxes are for user uploads and shouldn't be indexed in a public search engine.

### 9. Exponential backoff with sync suspension

**Decision:** When Mnemosyne requests fail, apply exponential backoff (2s → 4s → 8s → 16s → 32s) up to 5 retries. After 5 consecutive failures, suspend all sync activity (stop chunk tick timer, stop incremental POSTs, drain the incremental queue). The heartbeat timer continues at its normal 5-minute interval as a health probe. When a heartbeat succeeds, reset backoff and resume sync with a fresh full sync.

**Rationale:** The reference Lua plugin doesn't handle this — it just logs warnings and keeps trying. For a C server that blocks the event loop on each POST, we need to fail fast and stop wasting cycles against an unreachable Mnemosyne. The heartbeat is the lightest-weight request and already runs on a long interval, so it's the ideal canary to detect recovery without spamming.

**State machine:**

```
ACTIVE ──(5 consecutive failures)──▶ SUSPENDED
  │                                      │
  │◀──(heartbeat succeeds)───────────────┘
```

### 10. Queued incremental sync (not inline)

**Decision:** Incremental sync events (post, upload, delete) are queued in a bounded ring buffer (64 entries) and drained on the next chunk tick or heartbeat, not sent inline during the transaction handler.

**Rationale:** Sending an HTTP POST inside a transaction handler blocks the event loop while a user is waiting for their upload/post to complete. Queuing decouples content changes from network I/O. If the queue is full, the oldest entry is dropped — the periodic drift check will catch any missed changes within 15 minutes.

**Alternatives considered:**
- Inline POST in handler: Simpler but blocks the user's transaction on network I/O. A slow or unreachable Mnemosyne would make uploads feel laggy.
- Unbounded queue: Memory leak risk if Mnemosyne is down for a long time and content changes rapidly. Bounded queue with drop is safer.

### 11. DNS caching

**Decision:** Resolve the Mnemosyne hostname once on startup (and on SIGHUP config reload) and cache the IP address. All subsequent HTTP POSTs connect to the cached IP directly.

**Rationale:** `gethostbyname` is blocking and hits the DNS resolver on every call. During a chunked sync with 2-second ticks, that's a DNS lookup every 2 seconds. Caching avoids this and eliminates DNS as a source of event loop stalls. The cached address is refreshed on config reload or after sync suspension recovery.

### 12. Aggressive timeouts (2s connect, 5s total)

**Decision:** Set HTTP connect timeout to 2 seconds and total request timeout to 5 seconds, tighter than the reference plugin's defaults.

**Rationale:** Each POST blocks the event loop. A 2-second connect timeout means an unreachable Mnemosyne stalls the loop for at most 2 seconds per attempt. Combined with the backoff/suspension mechanism, the worst case is 5 attempts × 2 seconds = 10 seconds of total stall before suspension kicks in.

### 13. Tiered logging (log escalation, not every failure)

**Decision:** Log the first failure at WARN level. During backoff, log once per escalation level (not per retry). On suspension, log once at WARN with "will resume when heartbeat succeeds." On recovery, log once at INFO. During suspension, do not log skipped heartbeat failures.

**Rationale:** If Mnemosyne is down for hours, the log should have ~3 lines about it, not thousands. Operators can see "sync suspended" and "sync resumed" — that's enough.

## Risks / Trade-offs

- **[Blocking event loop during HTTP POST]** → Each chunk POST runs synchronously in the event loop. Mitigation: DNS caching eliminates resolver stalls. 2-second connect timeout caps the worst case. Chunks are small. 2-second tick spacing gives breathing room. Exponential backoff and suspension prevent sustained stalls against an unreachable server.

- **[No HTTPS]** → API key is sent in plaintext as a query parameter. Mitigation: `msv_` keys only authorize content push to a specific server. Mnemosyne content is public. HTTPS can be added later.

- **[Sync state file corruption]** → If the server crashes while writing the sync cursor file, the cursor may be corrupt. Mitigation: write to a temp file and rename (atomic on POSIX). On corrupt/missing cursor, start a fresh full sync. Discard stale cursors older than 1 hour.

- **[Large file trees]** → Servers with thousands of files will have many chunks. Mitigation: the 2-second tick and chunking prevent memory spikes. Pending directory list is bounded by actual filesystem depth. JSON buffers are allocated per-chunk and freed after POST.

- **[Incremental queue overflow]** → If Mnemosyne is down and content changes rapidly, the 64-entry ring buffer will drop old entries. Mitigation: the periodic drift check catches discrepancies within 15 minutes and triggers a targeted full sync.

- **[Mnemosyne extended outage]** → Sync suspended, heartbeat probing every 5 minutes. Mitigation: automatic recovery when heartbeat succeeds. Fresh full sync on resume ensures consistency. One log line on suspension, one on recovery.

## Open Questions

- **Sync cursor file format** — Use YAML (consistent with other config files) or a simpler format? YAML avoids adding a serializer but requires parsing with libyaml. **Resolved:** Using a simple `key: value` text format — avoids libyaml dependency for a single small file.

- **How does the `msv_` key get provisioned?** — **Resolved:** Operators register at https://agora.vespernet.net/login to obtain an `mop_`-prefixed operator key, then add their server through the Agora portal to receive an `msv_`-prefixed server API key. The default Mnemosyne instance is `tracker.vespernet.net:8980`.
