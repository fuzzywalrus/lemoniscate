## Why

Hotline servers that opt in to Mnemosyne allow clients to discover and search their content (message board posts, news articles, file listings) before connecting. Lemoniscate currently has no way to participate in this ecosystem. Adding Mnemosyne sync support makes the server discoverable and its content searchable across the broader Hotline community.

## What Changes

- Add a periodic content sync mechanism that pushes message board posts, threaded news articles, and file listings to a configured Mnemosyne index instance over HTTP.
- Add server configuration options for Mnemosyne URL, operator API key, server ID (operator-chosen slug), sync interval, and content type selection (files, news, message board, or any combination).
- Add a health-check probe on startup to verify connectivity to the Mnemosyne instance before enabling sync.
- Gracefully degrade when the Mnemosyne instance is unreachable (log warnings, retry on next interval, never block server operation).

**Note — partially discovered API:** The Mnemosyne server-side push API is not publicly documented. API probing of the live instance at `tracker.vespernet.net:8980` revealed an operator registration model (`POST /api/v1/register`), operator-scoped server management (`POST/PATCH /api/v1/operator/servers/{id}`), and `mop_`-prefixed API keys. The exact content push payload format is awaiting confirmation from fogWraith (the Mnemosyne operator). This proposal will be updated when the full API is known.

## Capabilities

### New Capabilities
- `mnemosyne-sync`: Periodic HTTP sync of server content (message board, news, files) to a Mnemosyne indexing instance. Covers operator authentication, content serialization to JSON, sync scheduling, error handling, and graceful degradation.

### Modified Capabilities
- `server-config`: New YAML configuration keys for Mnemosyne URL, operator API key, server ID, sync interval, and content type selection.
- `networking`: New outbound HTTP client capability for pushing content to the Mnemosyne REST API. The server currently only accepts inbound connections.

## Impact

- **New dependency**: Requires a minimal HTTP/1.1 implementation for outbound GET/POST/PATCH requests and JSON serialization. No external libraries.
- **Affected code**: `config_loader.c` (new config keys), `server.c` (sync timer in event loop), new source files for HTTP client and Mnemosyne sync logic.
- **Configuration**: Operator must register with Mnemosyne instance out-of-band (get approved, receive API key) before sync will work. The API key and server ID slug are then set in config.yaml.
- **Network**: New outbound HTTP traffic from the server process to the Mnemosyne instance. Firewall rules may need updating in deployment environments.
- **Existing behavior**: No changes to Hotline protocol handling or existing client connections. Sync runs independently on a background timer.
