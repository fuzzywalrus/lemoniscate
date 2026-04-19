## Why

The `modern` branch has advanced significantly past v0.1.4, adding Mnemosyne search integration, a platform abstraction layer, sync-only storage readers, GUI overhauls, and threaded news YAML parser fixes. The PPC `main` branch is stuck at 0.1.4 and missing all of this functionality. Porting these changes forward keeps PPC Tiger 10.4 at feature parity with the modern branch while respecting platform constraints (no Linux/Docker, CoreFoundation-only crypto/TLS).

## What Changes

- **Platform abstraction layer**: Headers for `platform_crypto.h`, `platform_encoding.h`, `platform_event.h`, `platform_tls.h` — PPC will use the macOS/CF implementations only (SecTransport TLS, CommonCrypto, kqueue)
- **Mnemosyne search sync**: HTTP client (`http_client.c`), JSON builder, and `mnemosyne_sync.c` for indexing server content to a Mnemosyne search instance. Includes chunked full sync with state machine, incremental sync via event hooks (64-entry ring buffer), drift detection, exponential backoff, DNS caching, and aggressive timeouts.
- **Sync-only storage readers**: `dir_threaded_news.c` and `jsonl_message_board.c` as **read-only parsers for Mnemosyne sync only**. These are NOT client-facing backends. The `janus-sync-reader` pattern parses existing flat text/YAML at sync time, preferring JSONL if present, falling back to best-effort flat text parsing.
- **Threaded news YAML parser fixes**: UTF-8 resilience (invalid byte sanitization), save/load round-trip correctness (`\r` escaping, `Date`/`ParentID` handling) in `threaded_news_yaml.c`
- **GUI overhaul**: Expanded `AppController` with new layout/tab system, Mnemosyne controls (URL, API key, index toggles), encoding popup (Mac Roman default), Vespernet integration, TLS cert generation
- **Server enhancements**: Expanded `server.c` with Mnemosyne hooks, config changes, stats updates
- **Tests**: `test_mnemosyne.c`, `test_mnemosyne_live.c`, `test_threaded_news.c`
- **Documentation**: Mnemosyne, security, feature parity updates

**⚠️ CRITICAL CONSTRAINT**: The client-facing news and message board backends (`flat_news.c`, `threaded_news_yaml.c`) MUST NOT be replaced or altered in how they serve Hotline clients. The modern branch learned this the hard way — an earlier attempt to swap in JSONL/directory backends broke the Hotline wire protocol (empty bodies, missing articles). All new storage formats are sync-only.

## Capabilities

### New Capabilities
- `mnemosyne-sync`: Chunked and incremental HTTP sync of server content (message board, news, files) to Mnemosyne search instances, with backoff, drift detection, and state persistence
- `sync-storage-readers`: Read-only parsers (`dir_threaded_news.c`, `jsonl_message_board.c`) that extract structured content from existing storage for Mnemosyne sync ONLY — never wired as client-facing backends
- `platform-abstraction`: Cross-platform headers with PPC-specific implementations (kqueue, SecTransport, CommonCrypto, CoreFoundation encoding)
- `http-client`: Lightweight HTTP GET client using raw BSD sockets for Mnemosyne API communication
- `gui-overhaul`: Expanded GUI with Mnemosyne controls, encoding settings, Vespernet layout, TLS cert generation

### Modified Capabilities
<!-- No existing specs to modify — specs/ is currently empty on main -->

## Excluded

- **Tracker V3 support**: Not yet functional on modern branch. Will be a separate change when ready.
- **Linux/Docker support**: Not applicable to PPC Tiger 10.4.

## Impact

- **Source files**: ~30 new/modified C source files, ~20 new/modified headers
- **Makefile**: Needs PPC-specific platform source selection (CF/kqueue paths, no epoll/OpenSSL)
- **Dependencies**: No new external dependencies for PPC (HTTP client uses sockets directly)
- **GUI (Cocoa)**: Major AppController changes — must verify Cocoa API compatibility with Tiger 10.4
- **Build system**: Platform abstraction layer means Makefile must conditionally compile macOS-only backends
- **Tests**: New test files need to compile on PPC GCC 4.0
- **Client-facing backends**: ZERO changes to flat_news.c or threaded_news_yaml.c wire-format behavior
