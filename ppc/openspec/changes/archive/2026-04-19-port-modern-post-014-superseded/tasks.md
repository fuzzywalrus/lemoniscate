## 1. Platform Abstraction Layer

- [x] 1.1 Copy platform headers (`platform_crypto.h`, `platform_encoding.h`, `platform_event.h`, `platform_tls.h`) from modern branch to `include/hotline/platform/`
- [x] 1.2 Port `crypto_commoncrypto.c` to `src/hotline/platform/` — verify CommonCrypto API availability on Tiger 10.4
- [x] 1.3 Port `encoding_cf.c` to `src/hotline/platform/` — replace existing `encoding.c` with platform-abstracted version
- [x] 1.4 Port `event_kqueue.c` to `src/hotline/platform/` — verify kqueue struct compatibility with Tiger
- [x] 1.5 Port `tls_sectransport.c` to `src/hotline/platform/` — replace existing `tls.c` with platform-abstracted version (KEPT Tiger-specific APIs: SSLNewContext, SecKeychainItemImport, SecIdentitySearchCreate)
- [x] 1.6 Update Makefile to compile platform sources from `src/hotline/platform/` (macOS backends only — NO epoll, NO OpenSSL)
- [x] 1.7 Build and verify platform layer compiles on PPC GCC 4.0

## 2. Mnemosyne Core Infrastructure

- [x] 2.1 Copy `http_client.h` to `include/hotline/` and port `http_client.c` to `src/hotline/`
- [ ] 2.2 Audit HTTP client byte-order handling for big-endian PPC (`htons`/`ntohs`/`htonl`/`ntohl`)
- [x] 2.3 Copy `json_builder.h` to `include/mobius/` and port `json_builder.c` to `src/mobius/`
- [x] 2.4 Copy `mnemosyne_sync.h` to `include/mobius/` and port `mnemosyne_sync.c` to `src/mobius/`
- [x] 2.5 Verify `mnemosyne_sync.c` uses platform-abstracted interfaces (not direct OpenSSL/epoll calls)
- [ ] 2.6 Port updated headers — only `config.h` needed update (Mnemosyne fields added); `tls.h` updated (platform_tls.h macros). Other headers (client.h, client_manager.h, stats.h, transaction.h, logger.h) have comment-only differences — skipped. `client_conn.h` HOPE changes deferred (different architecture, PPC version works).
- [x] 2.7 Port updated `server.c` — added Mnemosyne timer hooks (heartbeat 300s, periodic 900s) to existing kqueue loop. Kept raw kqueue (no platform_event.h migration — Tiger-specific).
- [x] 2.8 Port updated `config.c` and `config_loader.c` — add Mnemosyne config section support
- [x] 2.9 Port `password.c` to use platform crypto abstraction. `hope.c` DEFERRED — PPC has working Tiger-specific implementation using OpenSSL 0.9.7 directly; modern branch has complete rewrite with different architecture. Porting would cascade into `client_conn.h` changes.
- [ ] 2.10 Port updated `file_store.c` (no changes needed), `file_transfer.c` (resume_data cleanup fix)
- [x] 2.11 Port updated `main.c` — added Mnemosyne init (mn_sync_init), server pointer wiring, deregister on shutdown, version bump to 0.1.5
- [x] 2.12 Add all new sources to Makefile
- [ ] 2.13 Audit ALL ported code for endianness assumptions
- [x] 2.14 Build and verify Mnemosyne core compiles (all new .o files compile successfully)

## 3. Sync-Only Storage Readers

⚠️ **CRITICAL**: These modules are READ-ONLY parsers for Mnemosyne sync. They MUST NOT be wired as client-facing backends. The archived `janus-news-storage` change on the modern branch broke the Hotline wire protocol by making this mistake.

- [x] 3.1 Copy `dir_threaded_news.h` to `include/mobius/` and port `dir_threaded_news.c` to `src/mobius/`
- [x] 3.2 Copy `jsonl_message_board.h` to `include/mobius/` and port `jsonl_message_board.c` to `src/mobius/`
- [x] 3.3 Added `mnemosyne_sync.h` include to `transaction_handlers_clean.c` (kept PPC HOPE secure zone code intact)
- [x] 3.4 Wire sync readers into Mnemosyne sync subsystem ONLY — `mnemosyne_sync.c` references them internally
- [x] 3.5 **GUARDRAIL**: VERIFIED — `dir_threaded_news` not referenced in transaction handlers
- [x] 3.6 **GUARDRAIL**: VERIFIED — `jsonl_message_board` not referenced in transaction handlers
- [x] 3.7 **GUARDRAIL**: VERIFIED — `flat_news.c` and `threaded_news_yaml.c` remain sole client-facing backends (5 references in handlers)
- [x] 3.8 Add to Makefile and verify build — clean compile

## 4. YAML Parser Fixes (Client-Facing Bugfix)

These are bugfixes to the existing client-facing `threaded_news_yaml.c`, NOT a backend replacement.

- [x] 4.1 Port UTF-8 resilience fixes: invalid byte sanitization (replace with `?`) before YAML parsing
- [x] 4.2 Port save/load round-trip fixes: `\r` → `\n` escaping in `yaml_write_escaped()`
- [x] 4.3 Port `Date` field writing as flow sequence and `ParentArt`/`ParentID` handling
- [x] 4.4 Port updated `flat_news.c` — JSONL routing for sync, preserved client-facing flat text backend
- [x] 4.5 Build and verify — clean compile

## 5. GUI Overhaul

- [x] 5.1 Copied all GUI files from modern branch — uses Tiger-compatible APIs (deprecation warnings expected on modern macOS)
- [x] 5.2 Port `AppController.h` and `AppController.m` updates
- [x] 5.3 Port `AppController+GeneralActions.inc` — Mnemosyne disclosure section, encoding popup, TLS cert generation
- [x] 5.4 Port `AppController+LayoutAndTabs.inc` — tab layout with frame-based positioning
- [x] 5.5 Port `AppController+LifecycleConfig.inc` — Mnemosyne and encoding plist keys
- [x] 5.6 Port `ProcessManager.h` and `ProcessManager.m` updates
- [x] 5.7 Update `Info.plist` version to 0.1.5 (kept LSMinimumSystemVersion=10.4, NSHighResolutionCapable=false)
- [ ] 5.8 Verify encoding default is Mac Roman (not UTF-8) for classic Hotline client compatibility
- [x] 5.9 Build GUI target — 0 errors (deprecation warnings expected)

## 6. Tests, Documentation, and Verification

- [x] 6.1 Port `test_mnemosyne.c` to `test/`
- [x] 6.2 Port `test_mnemosyne_live.c` to `test/` (optional — requires network)
- [x] 6.3 Port `test_threaded_news.c` to `test/`
- [ ] 6.4 Add test targets to Makefile
- [ ] 6.5 Build and run tests on PPC
- [x] 6.6 Port `config/config.yaml.example` with Mnemosyne section
- [x] 6.7 Port updated `docs/FEATURES.md`, `docs/FEATURE_PARITY.md`, `docs/GUI_FEATURE_PARITY.md`
- [x] 6.8 Port `docs/MNEMOSYNE.md` and `docs/SECURITY.md`
- [ ] 6.9 Update `CHANGELOG.md` with PPC port notes
- [ ] 6.10 Update `README.md` with new feature descriptions
- [x] 6.11 Bump version in Makefile (main.c) and Info.plist to 0.1.5
- [x] 6.12 Full clean build on PPC G4 — server + GUI + app bundle all compile with GCC 4.0.1 on Leopard
- [x] 6.13 Launch GUI — fixed Tiger compat issues: NSApplicationDelegate/NSSplitViewDelegate protocols, NSBezelStyle/NSButtonType/NSWindowStyleMask/NSAlertStyle renames, NSLayoutConstraint/NSPopover replaced with frame-based layout, setAccessibilityLabel/setPlaceholderString removed (10.10+)
- [ ] 6.14 Test Mnemosyne sync with a live instance — fixed config_plist.c to read Mnemosyne plist keys
- [ ] 6.15 Test threaded news create/read/delete cycle — verify wire format unchanged
- [ ] 6.16 Test message board post and read cycle — verify wire format unchanged
- [x] 6.17 TLS certificate generation — added keychain password prompt to GUI, writes keychain.pass file

## Known Issues (follow-up tasks)

- [ ] Vespernet integration not working on PPC — needs investigation
- [ ] Help (?) buttons do nothing on Tiger — showHelpPopover: uses NSPopover (10.7+), replaced with alert but may still have issues
- [ ] Help button tooltips cropped by a few pixels on Tiger Aqua theme
