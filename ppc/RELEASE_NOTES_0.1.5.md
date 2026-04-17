# Lemoniscate v0.1.5 Release Notes

Big update bringing Mnemosyne search integration, UTF-8 resilience fixes, and GUI improvements across both branches. The PPC edition required significant porting work — the modern branch's platform abstraction layer had to be adapted for Tiger/Leopard's older APIs, and several 10.6+ Cocoa features needed Tiger compat shims.

Mnemosyne sync is the headline feature. Your Hotline server's files, news, and message board are now searchable through Vespernet's indexing service. The PPC edition is the first PowerPC Hotline server with search integration.

# 10.4 / 10.5 — PowerPC Edition

## v0.1.5 Changes

### Mnemosyne Search Integration
- Full Mnemosyne sync protocol implementation — heartbeat, full sync, incremental sync, drift detection, exponential backoff.
- HTTP client (`http_client.c`) built on raw BSD sockets — no libcurl dependency, works fine on Tiger.
- JSON builder for constructing sync payloads without a JSON library.
- Sync runs on kqueue timers: heartbeat every 5 minutes, drift detection every 15 minutes, initial full sync on first heartbeat.
- Configurable per-content-type indexing: files, threaded news, message board can each be toggled independently.
- API key sent as query parameter (matches Mnemosyne protocol spec).
- DNS caching and aggressive timeouts (2s connect, 5s read) so a dead Mnemosyne instance doesn't block the server.
- Graceful deregistration on shutdown.

### Sync-Only Storage Readers
- Added `dir_threaded_news.c` and `jsonl_message_board.c` as **read-only parsers for Mnemosyne sync only**.
- These are NOT wired into any client-facing code path. The flat text message board and YAML threaded news remain the sole backends for Hotline clients.
- The modern branch learned this the hard way — an earlier attempt to swap in JSONL/directory backends broke the Hotline wire protocol. We did not repeat that mistake.

### Platform Abstraction Layer
- New `include/hotline/platform/` headers: `platform_crypto.h`, `platform_encoding.h`, `platform_event.h`, `platform_tls.h`.
- PPC uses macOS-only backends: CommonCrypto, CoreFoundation encoding, kqueue, SecTransport.
- `password.c` switched from direct OpenSSL SHA-1 to platform crypto abstraction.
- `tls.h` updated to use platform macros instead of raw `#ifdef __APPLE__`.
- The existing Tiger TLS implementation was **kept as-is** — it uses `SSLNewContext`, `SecKeychainItemImport`, and `SecIdentitySearchCreate` which are the correct APIs for 10.4/10.5. The modern branch rewrote these to use 10.7+/10.8+ APIs that don't exist on Tiger.
- HOPE was also left untouched — the PPC implementation uses Tiger's OpenSSL 0.9.7 RC4 directly and works. Same protocol on the wire, different internals.

### YAML Parser Fixes
- UTF-8 resilience: invalid bytes are now sanitized (replaced with `?`) before feeding to libyaml, preventing total data loss on malformed articles.
- Save/load round-trip fixes: `\r` (Hotline wire format) is now escaped as `\n` in YAML output.
- `Date` field now written as flow sequence, `ParentArt`/`ParentID` parsing fixed.
- These were bugfixes from the modern branch's archived `yaml-parser-libyaml` change. Articles now display correctly in Navigator.

### GUI Improvements
- Mnemosyne disclosure section: enable checkbox, URL field, API key field, three index toggles (Files, News, Message Board).
- Text encoding popup (Mac Roman default — preserves classic Hotline client compatibility).
- TLS certificate generation now prompts for a keychain password and writes `keychain.pass` alongside the cert.
- Tab layout, Vespernet controls, and various settings UI updates from the modern branch.
- Several Tiger/Leopard compat fixes were needed:
  - `NSApplicationDelegate` / `NSSplitViewDelegate` protocols — conditional `#if` (10.6+ formal protocols).
  - `NSBezelStyleRounded`, `NSButtonTypeSwitch`, `NSWindowStyleMaskTitled`, etc — Tiger-era constants via `TigerCompat.h`.
  - `NSLayoutConstraint` / `NSPopover` — replaced with frame-based layout and `NSAlert`.
  - `setAccessibilityLabel:` / `setPlaceholderString:` — removed (10.10+ APIs that crash on Tiger).

### Config & Build
- Mnemosyne config fields added to `config.h` and read from both YAML (`config_loader.c`) and plist (`config_plist.c`).
- Makefile now auto-detects Tiger vs modern macOS — uses `/usr/local/include` and `gcc-4.0` on Tiger, `/opt/homebrew/include` and `cc` on modern.
- `PLATFORM_SRCS` / `PLATFORM_OBJS` added to Makefile for clean platform source selection.
- Version bumped to 0.1.5 in `main.c`, `Info.plist`.

### Known Issues
- Help `?` buttons don't show popovers on Tiger (tooltips work on hover).
- Help button icons clipped by a few pixels on Tiger Aqua theme.

---

# 10.11+ — Current, Intel/ARM Edition

## v0.1.5 Changes

### Mnemosyne Search Integration
- Full Mnemosyne sync protocol: chunked full sync with state machine and persistence, incremental sync via 64-entry ring buffer, heartbeat (5 min), drift detection (15 min), exponential backoff with sync suspension after 5 consecutive failures.
- Heartbeat serves as recovery canary — when Mnemosyne comes back after an outage, the first successful heartbeat resumes sync automatically.
- HTTP client and JSON builder — zero external dependencies, raw BSD sockets.
- Message board sync uses `janus-sync-reader` pattern: parses flat text `MessageBoard.txt` at sync time, prefers JSONL if present, falls back to best-effort flat text parsing. Never replaces the client-facing backend.
- DNS caching, tiered logging (reduces log spam during extended outages), sync cursor persistence for crash recovery.
- SIGHUP reload support — Mnemosyne timers are re-registered with new config on config reload.

### Platform Abstraction Layer
- New cross-platform headers for crypto, encoding, events, and TLS.
- macOS: CommonCrypto, CoreFoundation, kqueue, SecureTransport backends.
- Linux: OpenSSL, epoll + timerfd, static encoding lookup table backends.
- Platform selection via `uname -s` in Makefile — `PLATFORM_SRCS` chosen at build time.
- `password.c` and `hope.c` both use platform crypto abstraction (no direct OpenSSL calls in common code).

### Directory-Threaded News & JSONL Message Board
- `dir_threaded_news.c`: filesystem-backed threaded news using directory hierarchy with per-article JSON files.
- `jsonl_message_board.c`: JSONL-based structured post storage.
- Both are sync-only backends consumed by Mnemosyne — not client-facing. The flat text and YAML backends remain the wire-format sources for all Hotline client operations.

### YAML Parser Fixes
- UTF-8 sanitization before libyaml parsing prevents data loss on malformed articles.
- Save/load round-trip: `\r` escaping, `Date` field flow sequence, `ParentID` scalar handling.
- Deployed to VPS — article bodies now display correctly in Navigator.

### GUI Improvements
- Mnemosyne disclosure section with URL, API key, and per-content-type index toggles.
- Text encoding popup selector (Mac Roman / UTF-8).
- TLS certificate generation with keychain password prompt.
- Vespernet layout integration.
- Expanded tab layout for server controls, logs, accounts, online users, files, news.

### Docker & Linux
- Multi-stage Dockerfile for containerized deployment.
- Docker documentation for Linux, macOS, and Windows hosts.
- Linux platform support verified on Docker.

### Config & Build
- Mnemosyne config section in YAML and plist loaders.
- New docs: `MNEMOSYNE.md`, `SECURITY.md`, updated feature parity docs.
- `config.yaml.example` updated with Mnemosyne section.
- Version 0.1.5.
