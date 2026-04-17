# Changelog

All notable changes to this project are documented in this file.

## 0.1.7 — 2026-04-16

### Added

- **Persistent public chat history (extension)**: Opt-in server-side persistence of public chat. Capability bit 4 (`HL_CAPABILITY_CHAT_HISTORY`) is negotiated at login; capability-aware clients fetch backlog via the new `TranGetChatHistory` (700) transaction with cursor-based pagination (BEFORE / AFTER / LIMIT, default 50, max 200). Private chats (`FieldChatID` present) are never persisted. See [docs/Capabilities-Chat-History.md](docs/Capabilities-Chat-History.md) and [openspec/changes/chat-history/](openspec/changes/chat-history/).
- **JSONL storage backend**: One append-only `ChatHistory/channel-N.jsonl` per channel under `FileRoot`, with a per-channel in-memory index for O(log n) cursor queries. Crash-safe — the startup scan trims any partial last-line garbage via `ftruncate`. PPC-safe (`_FILE_OFFSET_BITS=64`, `ftello`/`fseeko`, `fgets`, no pthreads).
- **Optional ChaCha20-Poly1305 body encryption at rest**: Set `ChatHistory.encryption_key` to a 32-byte key file. Bodies are stored as `"ENC:<base64(nonce||ciphertext||tag)>"`; metadata (id, ts, nick, flags) stays plaintext so external tooling and the index scan still work.
- **Tombstone sidecar**: `lm_chat_history_tombstone()` records redactions in `ChatHistory/tombstones.jsonl`; queries return the entry with body cleared and `HL_CHAT_FLAG_IS_DELETED` set.
- **Retention pruning**: Configurable per-channel message cap (`max_messages`) and age cap (`max_days`). Runs once on startup and every 3600s thereafter via the existing idle-check timer.
- **Per-connection rate limiting**: Token bucket on `TranGetChatHistory` (defaults: 20-token capacity, 10 tokens/sec refill). Over the limit, the server replies with `"chat history rate limited"`.
- **Legacy fallback**: When `legacy_broadcast: true`, clients without the chat-history capability bit receive the last `legacy_count` messages as ordinary `TranChatMsg` (106) right after login, formatted with the right `\r %nick%: %body%` (or action / server-msg variants) and transcoded to the wire encoding.
- **Permission bit 56 (`ACCESS_READ_CHAT_HISTORY`)**: New per-account permission gating the read path. Falls back to bit 9 (`ACCESS_READ_CHAT`) when bit 56 is unset.
- **New config block**: `ChatHistory:` in `config.yaml` (and equivalent flat `ChatHistory*` keys in `config.plist`) — `enabled`, `max_messages`, `max_days`, `legacy_broadcast`, `legacy_count`, `encryption_key`, `rate_capacity`, `rate_refill_per_sec`. CLI/YAML/plist paths all wired; GUI surface deferred to a separate PR.
- **Chat history tests**: 10 new unit tests in `test/test_chat_history.c` covering pagination (BEFORE / AFTER / range / empty), crash-truncate recovery, tombstone semantics, count- and age-based prune, and encryption round-trip across close/reopen. All pass; full suite 52/52 (no regressions).

## 0.1.5 — 2026-04-03

### Added

- **Mnemosyne search integration**: Server content (files, news) is synced to a Mnemosyne indexing instance, making the server discoverable and searchable via Hotline Navigator. Default instance: `tracker.vespernet.net:8980`. Register at `agora.vespernet.net` for an API key.
- **HTTP client**: Minimal blocking HTTP POST/GET client for Mnemosyne sync and search verification.
- **JSON builder**: String escaping and dynamic buffer utilities for building JSON payloads without a library dependency.
- **Mnemosyne integration tests**: 18 live integration tests against the VesperNet Mnemosyne instance (requires `MSV_API_KEY` env var, skips gracefully without it).
- **New config section**: `Mnemosyne` block in `config.yaml` with `url`, `api_key`, `index_files`, `index_news`.

### Fixed

- **File transfer memory leak**: `resume_data` was not freed when transfers were deleted or the manager was destroyed.
- **File store missing free function**: Added `hl_file_store_free()`.

## 0.1.4 — 2026-03-29

### Added

- **HOPE E2E TLS requirement**: New `E2ERequireTLS` config option. Clients must connect via TLS to see E2E-prefixed content, ensuring file transfers are also encrypted on the wire.
- **Self-signed TLS certificate generation**: "Generate Self-Signed..." button in the TLS settings creates RSA 2048-bit cert/key in the config directory and auto-fills the TLS fields.
- **Collapsible disclosure sections**: Settings panel redesigned with collapsible disclosure triangles, matching modern macOS design patterns.
- **Help popovers**: Apple-standard (?) help buttons on all settings with full explanatory text in NSPopover.
- **Tooltips**: All settings controls now have descriptive hover tooltips.

### Fixed

- **TLS code signing crash**: Switched libyaml from dynamic to static linking. Signed builds no longer fail at runtime due to Team ID mismatch on the Homebrew dylib.
- **Self-signed key format**: Certificate generation now uses `openssl genrsa` (traditional RSA format) instead of `openssl req -newkey` (PKCS#8), fixing SecureTransport import failure.
- **Wizard button clipped**: "Finish & Start" renamed to "Finish and Start" and widened to prevent text truncation.

### Changed

- **Removed Mobius references**: Cleaned comments, URLs, and "Maps to: Go..." annotations across all source and header files. Lemoniscate is now positioned as its own project.
- **Documentation overhaul**: Updated README, all docs, created SECURITY.md with plain-speak and technical HOPE/TLS breakdown.

## 0.1.3 — 2026-03-22

### Fixed

- **Path traversal in folder uploads**: client-supplied path segments are now validated against traversal attacks (`..`, embedded `/`) using the existing `is_safe_path_component` check.
- **Missing handler registrations**: `handle_upload_file` and `handle_download_folder` were defined but never registered — single-file uploads and folder downloads now work.
- **Filename validation in make_alias**: filenames are now checked with `is_safe_filename` before creating symlinks, preventing path escape.
- **user_name null termination**: `user_name` is now explicitly null-terminated after every `memcpy`, preventing stale data when users change their nickname to something shorter.

## 0.1.1 — 2026-03-21

### Fixed

- **Tracker registration**: default tracker port corrected from 5498 to 5499 (standard Hotline tracker port).
- **Tracker diagnostics**: DNS resolution, socket, and send failures now log to stderr instead of failing silently.
- **Tracker return values**: `hl_tracker_register_all` now returns the count of successful registrations; callers log actual success/failure.
- **Config booleans**: YAML `EnableTrackerRegistration`, `EnableBonjour`, and `PreserveResourceForks` now accept case-insensitive values (`true`/`True`/`TRUE`/`yes`/`Yes`/`YES`).
- **PassID fallback**: tracker PassID generation falls back to `time() ^ getpid()` if `/dev/urandom` is unavailable.
- **strncpy safety**: explicit null-termination after all strncpy calls in tracker address parsing.
- **Sample config**: updated `config/config.yaml` with correct tracker port (5499), added `EnableTrackerRegistration` field, real tracker addresses.

### Added

- **Tiger (10.4) GUI compatibility**: `TigerCompat.h/.m` provides `NSInteger`/`NSUInteger` typedefs, `stringByReplacingOccurrencesOfString:withString:`, `componentsSeparatedByCharactersInSet:`, `newlineCharacterSet`, and `StatusDotView` (replaces 10.5-only `NSBox` `setFillColor:`/`setCornerRadius:`).
- Removed 10.5-only `NSSplitViewDividerStyleThin` usage from GUI layout.

### Documentation

- Refreshed `README.md` with actionable CLI and GUI quickstarts, updated build guidance, and an explicit feature status matrix (implemented/partial/planned).
- Updated `docs/SERVER.md` to match current behavior:
  - documented `--bind` as `--port` alias,
  - clarified current `SIGHUP` reload limitation,
  - marked threaded news and transfer-port file operations as partial/stubbed.
- Added `docs/GUI.md` covering app build/run flow, bundle layout, architecture checks, current GUI limitations, and troubleshooting.
- Added `docs/README.md` as a docs index for operator, GUI, and developer navigation.
- Linked docs consistently across README/server/gui docs for faster onboarding.
