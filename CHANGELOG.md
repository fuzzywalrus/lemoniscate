# Changelog

All notable changes to this project are documented in this file.

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
