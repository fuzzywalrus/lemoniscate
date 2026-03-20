# Changelog

All notable changes to this project are documented in this file.

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
