## Why

Hotline servers that opt in to Mnemosyne allow clients to discover and search their content (message board posts, news articles, file listings) before connecting. Lemoniscate currently has no way to participate in this ecosystem. Adding Mnemosyne sync support makes the server discoverable and its content searchable across the broader Hotline community.

## What Changes

- Add chunked content sync to a Mnemosyne instance via HTTP POST, supporting files and threaded news. Each content type syncs independently with a chunked protocol (sync_id + chunk_index + finalize). Message board sync is deferred — the flat message board (`MessageBoard.txt`) stores posts as raw text without structure (no post IDs, no pagination, no deletion). Syncing it requires a text parser to extract structured posts, which is out of scope for this change.
- Add incremental sync triggered by content changes — uploads, file deletions, and news articles are pushed immediately without waiting for the periodic cycle.
- Add a heartbeat mechanism that sends server metadata and content counts to Mnemosyne every 5 minutes.
- Add periodic drift detection (every 15 minutes) that compares cached content counts with actual counts and triggers targeted resync when they diverge.
- Add deregistration on server shutdown (`POST /api/v1/sync/deregister`).
- Add server configuration options for Mnemosyne URL, server API key (`msv_`-prefixed), and per-content-type indexing toggles (files, news, message board).
- Gracefully degrade when the Mnemosyne instance is unreachable (log warnings, retry on next interval, never block server operation).

**API confirmed** from fogWraith's reference Janus plugin (`mnemosyne.lua`). Authentication uses `?api_key=msv_...` query parameter. Content is pushed via separate endpoints per type under `/api/v1/sync/`.

## Capabilities

### New Capabilities
- `mnemosyne-sync`: Chunked and incremental HTTP sync of server content (message board, news, files) to a Mnemosyne indexing instance. Covers authentication, heartbeat, chunked full sync, incremental event-driven sync, drift detection, content serialization to JSON, sync state persistence, and graceful degradation.

### Modified Capabilities
- `server-config`: New YAML configuration keys for Mnemosyne URL, server API key, and content type toggles.
- `networking`: New outbound HTTP POST capability for pushing content to the Mnemosyne sync API. Uses the platform HTTP client (already built for linux-platform-support, or BSD sockets directly on macOS).

## Impact

- **New dependency**: None. Uses the existing minimal HTTP client (or builds one if implementing before the platform abstraction is in place). JSON serialization via snprintf.
- **Affected code**: `config_loader.c` (new config keys), `server.c` (sync timers in event loop), new source files for Mnemosyne sync state machine and content serialization.
- **Configuration**: Operator registers at https://agora.vespernet.net/login to obtain an `msv_`-prefixed API key, then sets it in config.yaml. The default Mnemosyne instance is `tracker.vespernet.net:8980`.
- **Network**: New outbound HTTP POST traffic from the server process to the Mnemosyne instance. Heartbeat every 5 minutes, chunked sync every 15 minutes (or on content change). Firewall rules may need updating.
- **Event loop**: Three new timers — heartbeat (5 min), periodic drift check (15 min), and chunk tick (2 sec during active sync). Chunk tick is only active while a sync is in progress.
- **Existing behavior**: No changes to Hotline protocol handling or existing client connections. Sync runs in the main event loop alongside normal transaction processing.
