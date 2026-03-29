# Integration Notes: modern branch vs main

This file tracks what was and wasn't integrated from the `main` branch into the `modern` branch, and why. The `modern` branch targets macOS 10.11+ with zero deprecation warnings, using current APIs throughout.

## Sync process

When new commits land on `main`:

1. `git fetch origin main`
2. `git diff HEAD..origin/main` — review what changed
3. Manually re-implement changes on `modern`, replacing deprecated APIs with modern equivalents
4. Update the "Integrated from main" section below with what was ported
5. `git merge origin/main -s ours` — record commit parity without overwriting files
6. The merge commit message should reference this file

### Last synced

- **main commit:** `ea86c90` (v0.1.3 + Documentation update, 2026-03-21)
- **Parity merge on modern:** `git merge origin/main -s ours`

## Modern branch exclusive features

These features exist only on the `modern` branch and have no counterpart on `main`:

### HOPE Encryption
- **HOPE secure login** (`hope.c`, `hope.h`) — Challenge-response MAC authentication replacing plaintext passwords
- **HOPE transport encryption** — RC4 stream cipher for the transaction channel after authentication
- **E2E file content gating** — Files/folders with a configurable prefix are hidden from non-encrypted clients
- **E2E TLS requirement** (`E2ERequireTLS` config) — Optionally requires TLS connection for E2E content visibility, ensuring file transfers are also encrypted

### TLS Enhancements
- **Self-signed certificate generation** — GUI button generates RSA 2048-bit cert via `/usr/bin/openssl`, auto-fills TLS fields
- **Traditional RSA key format** — Uses `openssl genrsa` for SecureTransport compatibility

### GUI Overhaul
- **Collapsible disclosure sections** — Settings panel uses disclosure triangles instead of fixed NSBox sections
- **Help popovers** — Apple-standard (?) help buttons with NSPopover explanations on every setting
- **Tooltips** — All controls have descriptive hover tooltips
- **Consistent spacing** — 21px indented content under disclosure headers, matching modern design patterns
- **Static libyaml linking** — No runtime Homebrew dependencies for signed distribution

### Build & Signing
- **Static libyaml** — Switched from dynamic to static linking to avoid Team ID mismatch in signed builds
- **Developer ID signing** — Full codesign with hardened runtime

## Integrated from main

### Security Fixes
- **Path traversal in folder uploads** (`server.c`) — client-supplied path segments now validated with `hl_is_safe_path_component` to prevent `../` escape attacks.
- **Filename validation in make_alias** (`transaction_handlers.c`) — `is_safe_filename` check added before creating symlinks.
- **`hl_is_safe_path_component` made public** (`file_path.h`, `file_path.c`) — previously static, now exported for use in `server.c`.

### Bug Fixes
- **user_name null termination** (`server.c`, `transaction_handlers.c`) — `cc->user_name[len] = '\0'` added after every `memcpy` to prevent stale data when users shorten their nickname.
- **Handler registration** (`transaction_handlers.c`) — `handle_upload_file` and `handle_download_folder` wired into dispatch table (also done independently on modern branch).

### New Features
- **DeletableTableView** (`AppController.m`) — NSTableView subclass that forwards Delete/Backspace key to a target+action for keyboard-driven row deletion.
- **IPv4 validation** (`AppController.m`) — `isValidIPv4()` helper for ban list input validation.
- **Polling rate control** (`AppController.h`, `.inc` files) — configurable refresh interval for online users / news with `_pollingRatePopup`.
- **News auto-refresh timer** (`AppController.h`) — `_newsRefreshTimer` for periodic news updates.
- **GUI enhancements** (various `.inc` files) — new features, layout improvements, table notifications.

### Metadata
- **Version bump** to 0.1.3 (`main.c`, `Info.plist`).
- **CHANGELOG.md** — 0.1.3 entry with security fix notes.
- **Icon rename** — `Lemoniscate.icns` → `lemoniscate.icns` (lowercase).

## Skipped / Overridden (kept modern branch version)

### TigerCompat backfill categories
**Files:** `TigerCompat.h`, `TigerCompat.m`
**Reason:** Main re-added NSInteger/NSUInteger typedefs, NSString category methods (`stringByReplacingOccurrencesOfString:`, `componentsSeparatedByCharactersInSet:`), and NSCharacterSet `newlineCharacterSet`. These are all native since macOS 10.5. On modern macOS, the category methods override system implementations which is fragile and unnecessary. Modern branch keeps only `StatusDotView`.

### Deprecated AppKit constants
**Files:** `AppController.m`, all `.inc` files
**Reason:** Main uses deprecated constant names (`NSRoundedBezelStyle`, `NSRightTextAlignment`, `NSOnState`, `NSSwitchButton`, `NSTitledWindowMask`, etc.). Modern branch uses their current equivalents (`NSBezelStyleRounded`, `NSTextAlignmentRight`, `NSControlStateValueOn`, `NSButtonTypeSwitch`, `NSWindowStyleMaskTitled`, etc.). These are compile-time constants that resolve to the same values — no runtime impact.

### NSRunAlertPanel
**Files:** Various `.inc` files
**Reason:** Deprecated since 10.10. Modern branch uses `NSAlert` objects instead.

### Deprecated NSFileManager methods
**Files:** Various `.inc` files
**Reason:** `removeFileAtPath:handler:`, `copyPath:toPath:handler:`, `directoryContentsAtPath:`, etc. replaced with modern equivalents (`removeItemAtPath:error:`, etc.).

### OpenSSL dependency
**File:** `password.c`
**Reason:** Modern branch uses CommonCrypto (`CC_SHA1_*`) which is part of the macOS SDK — no external dependency needed. Main still uses OpenSSL which requires Homebrew and produces deprecation warnings.

### OSAtomic / libkern
**Files:** `client_manager.c`, `client.m`
**Reason:** Deprecated since 10.12. Modern branch uses C11 `<stdatomic.h>` (`atomic_fetch_add`, `atomic_thread_fence`).

### CFPropertyListCreateFromXMLData
**File:** `config_plist.c`
**Reason:** Deprecated since 10.10. Modern branch uses `CFPropertyListCreateWithData`.

### Protocol conformance removal
**File:** `AppController.h`
**Reason:** Main doesn't declare `<NSApplicationDelegate, NSSplitViewDelegate>` on the interface. Modern branch keeps it to suppress incompatible-type warnings.

### Makefile / build flags
**File:** `Makefile`
**Reason:** Modern branch uses C11 (`-std=c11`), arch-aware Homebrew paths, `-mmacosx-version-min=10.11`, and no `-lcrypto`. Main uses C99 and hardcoded paths.
