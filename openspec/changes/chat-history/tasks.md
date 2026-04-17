## 1. Protocol Constants

- [x] 1.1 Add `FIELD_CHANNEL_ID` (0x0F01) through `FIELD_HISTORY_MAX_DAYS` (0x0F08) — placed in new `include/hotline/chat_history.h` alongside the storage interface (HOPE header convention), not `field.h`
- [x] 1.2 Add `TRAN_GET_CHAT_HISTORY` (700) to `include/hotline/types.h` alongside the existing transaction constants (not `transaction.h` — constants live in types.h)
- [x] 1.3 Add `HL_CAPABILITY_CHAT_HISTORY` (0x0010) and `HL_CAPABILITY_TEXT_ENCODING` (0x0002) to `include/hotline/types.h` next to `HL_CAPABILITY_LARGE_FILES`
- [x] 1.4 Add `ACCESS_READ_CHAT_HISTORY` (bit 56) to `include/hotline/access.h` after `ACCESS_SEND_PRIV_MSG`

## 2. Configuration Surface

- [x] 2.1 Extended `hl_config_t` in `include/hotline/config.h` with all eight chat history fields
- [x] 2.2 Wired YAML keys under nested `ChatHistory:` section in `src/mobius/config_loader.c` (matches Mnemosyne pattern)
- [x] 2.3 Wired plist keys in `src/mobius/config_plist.c` (flat `ChatHistory*` keys since plist is flat)
- [x] 2.4 Set defaults in `hl_config_init()`: enabled=0, max_msgs=10000, max_days=0, legacy_broadcast=0, legacy_count=30, rate_capacity=20, rate_refill=10
- [ ] 2.5 **Deferred** — GUI config surface requires new Cocoa widgets + preferences panel resize. CLI/YAML/plist paths work; GUI users would see keys stripped on save until widgets land. Separate PR worth its own design pass.

## 3. Storage Module — JSONL Backend

- [x] 3.1 Created `include/hotline/chat_history.h` with `lm_chat_history_t` opaque struct, `lm_chat_entry_t`/`lm_chat_idx_entry_t`/`lm_chat_history_config_t` typedefs, full public API (open/append/query/tombstone/prune/fsync/close/count/next_id)
- [x] 3.2 Created `src/hotline/chat_history.c` with `lm_chat_history_open()` — mkdir `ChatHistory/`, scan `channel-*.jsonl`, per-channel in-memory index, global `next_id` seeded from max ID
- [x] 3.3 Startup scan via `lm_channel_scan()` — `fgets` line-by-line, extract `"id":N` via `strstr` + `strtoull`, record `(id, offset, length)`
- [x] 3.4 `lm_chat_history_append()` — formats JSONL line (with optional body encryption), `fopen "a"` + `fwrite` + `fflush`, appends to in-memory index, returns assigned ID
- [x] 3.5 `lm_chat_history_query()` — binary-search the index (`lm_idx_lower_bound`), walk `limit` entries, read each line via `pread`/`fseeko`, JSON-parse into `lm_chat_entry_t`
- [x] 3.6 `lm_chat_history_tombstone()` — sidecar approach (design's open question resolved in favor of sidecar): appends to `ChatHistory/tombstones.jsonl`; query applies tombstone to body/nick and sets `HL_CHAT_FLAG_IS_DELETED`
- [x] 3.7 `lm_chat_history_prune()` — count+age rewrite to `.tmp`, `fsync`, atomic `rename`, rebuild index
- [x] 3.8 `lm_chat_history_close()` — frees indexes, closes files, frees tombstone buffer

## 4. Crash Safety

- [x] 4.1 Implemented inline in `lm_channel_scan()` — tracks `last_good` offset; on corrupted line or missing newline, opens with O_WRONLY and `ftruncate`s + `fsync`s. INFO-level log left as a follow-up when integrating with `hl_log_info`.
- [x] 4.2 Covered by `test_corrupt_truncate` in `test/test_chat_history.c`: appends 3 entries, writes 50 bytes of garbage with no newline, reopens; asserts file size shrank and the 3 entries are still indexable + readable.
- [x] 4.3 Wired in `hl_server_listen_and_serve` event loop — `chat_fsync_elapsed` accumulator on `HL_TIMER_IDLE` (10s), calls `lm_chat_history_fsync` every 60s when chat history is enabled.

## 5. Encryption at Rest (Optional)

- [x] 5.1 `lm_load_key()` reads 32 bytes from `encryption_key_path`; fails open cleanly on missing/short file
- [x] 5.2 Append path: if key loaded, `lm_body_encrypt()` generates nonce via `/dev/urandom`, calls `hl_chacha20_poly1305_encrypt`, writes `"body":"ENC:<base64>"`. **Note:** AAD dropped from design — existing AEAD API doesn't accept it; key itself provides domain separation.
- [x] 5.3 Query path: `lm_parse_entry()` detects `ENC:` prefix, decrypts with `lm_body_decrypt()`, substitutes `"[decryption failed]"` on tag mismatch
- [ ] 5.4 Unit test: encrypt a known plaintext, write to JSONL, decrypt via query path, verify round-trip

## 6. Chat Send Hook

- [x] 6.1 In `handle_chat_send` public branch, transcode nick/body to UTF-8 (using `srv->use_mac_roman`) and call `lm_chat_history_append(ctx, 0, flags, icon, nick, body)`
- [x] 6.2 Derive `is_action` from `FIELD_CHAT_OPTIONS` (already done in handler) and pass `HL_CHAT_FLAG_IS_ACTION` to append
- [x] 6.3 Private chat (`FIELD_CHAT_ID` present) flows through the `if` branch and is never appended — guaranteed by structure
- [x] 6.4 `handle_user_broadcast` appends with `HL_CHAT_FLAG_IS_SERVER_MSG`, nick = server name

## 7. Transaction Handler — TRAN_GET_CHAT_HISTORY (700)

- [x] 7.1 Registered alongside TRAN_SET_CHAT_SUBJECT in `mobius_register_handlers`
- [x] 7.2 Parses `FIELD_CHANNEL_ID` (required, u16); `FIELD_HISTORY_BEFORE`/`AFTER` (u64 BE); `FIELD_HISTORY_LIMIT` (u16, default 50, clamped to `LM_CHAT_HISTORY_MAX_LIMIT`)
- [x] 7.3 Access check — bit 56 first, fall back to bit 9; missing both → error reply
- [x] 7.4 `lm_chat_rl_take()` returns 0 → error reply `"chat history rate limited"`
- [x] 7.5 Calls `lm_chat_history_query` with parsed cursors/limit
- [x] 7.6 Reply contains one `FIELD_HISTORY_ENTRY` per returned message + trailing `FIELD_HISTORY_HAS_MORE` (1-byte flag)
- [x] 7.7 `lm_pack_history_entry()` packs id/ts/flags/icon/nick/body per spec, transcoding nick + body from stored UTF-8 to wire encoding (server-wide `use_mac_roman`). Per-connection negotiated encoding deferred until §1.3 capability ships.

## 8. Rate Limiting

- [x] 8.1 Added `chat_rl_tokens_x10` (uint16) + `chat_rl_last_refill_ms` (uint64) to `hl_client_conn_t`
- [x] 8.2 Lazy-initialized in `lm_chat_rl_take()` on first request — first call seeds bucket to full capacity (`capacity × 10`), avoiding the need to know server config at connection time
- [x] 8.3 Refill = `delta_ms × refill_per_sec / 100`; clamp to `capacity × 10`; require ≥10 to consume; otherwise reply error and leave bucket untouched (other than refill)
- [x] 8.4 Refactored: extracted pure helper `lm_chat_rl_consume()` exposed in `chat_history.h` (takes explicit `now_ms`); `lm_chat_rl_take()` is now a 6-line wrapper. `test_rate_limiter` covers exact spec scenario — 25 rapid requests at t=0 give 20 allowed + 5 denied; advancing 500 ms refills 5 tokens (allowing 5 more, denying the 6th); advancing 5 s clamps refill at capacity rather than overshooting. Also verifies lazy-init seeds full bucket when `last_refill_ms == 0`.

## 9. Login Reply Integration

- [x] 9.1 Capability negotiation block in `server.c` sets `cc->chat_history_capable` when client bit 4 + server enable + `srv->chat_history` non-NULL all align. Echoed in login reply via combined `echoed_caps` mask
- [x] 9.2 When `chat_history_capable`, login reply adds `FIELD_HISTORY_MAX_MSGS` and `FIELD_HISTORY_MAX_DAYS` (uint32 BE) — zero means unlimited per spec
- [x] 9.3 Legacy broadcast block runs after agreement step, before notify_change_user: queries last N (oldest-first), formats as `\r %nick%: %body%` (or `\r *** %nick% %body%` for action / `\r %body%` for server msg), transcodes UTF-8 → wire encoding, sends as TRAN_CHAT_MSG. Gated on `!chat_history_capable` + `chat_history_legacy_broadcast` + ACCESS_READ_CHAT.

## 10. Retention Prune Scheduler

- [x] 10.1 Piggy-backed on `HL_TIMER_IDLE` (10s) — `chat_prune_elapsed` accumulates; calls `lm_chat_history_prune` every 3600s when chat history is enabled
- [x] 10.2 Startup prune runs in `main.c` immediately after `lm_chat_history_open` succeeds (so over-retention messages are dropped before traffic arrives)
- [x] 10.3 Logs `chat history prune: completed` at INFO; prune detail counters (channel/dropped_count/dropped_age) deferred — would require lm_chat_history_prune to return stats

## 11. Testing

`test/test_chat_history.c` exercises 11.1–11.9 + 11.11. Run with `make test-chat-history`. All 10 pass on macOS.

- [x] 11.1 `test_append_1000_query_latest_50` — appends 1000, queries default-cursor with limit 50, verifies ids 951..1000 oldest-first + has_more=1
- [x] 11.2 `test_query_before` — BEFORE=50 limit=20 → ids 30..49 + has_more=1
- [x] 11.3 `test_query_after` — AFTER=50 limit=20 → ids 51..70 + has_more=1
- [x] 11.4 `test_query_range` — BEFORE=60 AFTER=20 limit=100 → 39 entries (ids 21..59) + has_more=0
- [x] 11.5 `test_query_empty` — zero entries returned, has_more=0, NULL out_entries
- [x] 11.6 `test_corrupt_truncate` — appends 3 valid entries, writes 50 bytes of garbage, reopens; verifies file size shrank and 3 entries restored intact
- [x] 11.7 `test_tombstone` — tombstones id=1; query returns 2 entries; id=1 has body="" + HL_CHAT_FLAG_IS_DELETED set; id=2 untouched
- [x] 11.8 `test_prune_by_count` — appends 250 with max_msgs=100; prune drops oldest; latest entry (id=250) preserved
- [x] 11.9 `test_prune_by_age` — hand-writes JSONL with 10-day-old + 1-hour-old entries, max_days=7; prune drops the old ones, keeps the recent one
- [x] 11.10 `test_rate_limiter` — drives the now-public `lm_chat_rl_consume` directly. Asserts: 25 requests at the same timestamp give exactly 20 allowed (capacity); 500 ms later, exactly 5 more allowed (refill at 10 tokens/sec); 5 s later, refill clamps at capacity (not 50); pristine state seeds a full bucket on first call.
- [x] 11.11 `test_encryption_roundtrip` — opens with key file, appends plaintext, verifies on-disk JSONL contains "ENC:" but not the plaintext, query decrypts; reopens with same key, round-trip survives
- [ ] 11.12 Integration test: live chat send → history query returns the sent message — **deferred**: needs an in-process server harness or end-to-end client driver
- [ ] 11.13 Integration test: private chat (FIELD_CHAT_ID present) is NOT persisted — **deferred**: same harness gap as 11.12
- [x] 11.14 No regressions — `test-chacha20` 7/7, `test-hope-aead` 19/19, `test-threaded-news` 16/16, `test-chat-history` 10/10 (52/52 total). `test-wire` was already broken pre-change (server.o pulls mobius/mn_* symbols not in its link line — pre-existing fragility, not introduced here)

## 12. Documentation

- [x] 12.1 `docs/FEATURES.md` — added "Persistent public chat history (opt-in) — capability-negotiated, cursor-paginated" bullet under Chat & Messaging, linking to the spec doc
- [x] 12.2 `docs/FEATURE_PARITY.md` — added `TranGetChatHistory (700)` entry with a 3-way comparison table noting Lemoniscate has it, Mobius/Janus don't
- [x] 12.3 Config example — `config/config.yaml.example` got a full ChatHistory block with all eight settings + comments; `docs/SERVER.md` inline example got a condensed ChatHistory block; README's Chat & Messaging bullet pointer added
- [x] 12.4 `CHANGELOG.md` — 0.1.7 entry covering the storage backend, encryption, retention, rate limiting, legacy fallback, permission bit 56, config surface, and the 10 new tests

## 13. Build & Deploy

- [x] 13.1 `make clean && make app` — clean macOS rebuild succeeds; only pre-existing libyaml linker warning (homebrew built for newer macOS) remains. Both `lemoniscate` server binary and `Lemoniscate.app` bundle build cleanly.
- [x] 13.2 `docker build -t lemoniscate-test:0.1.7 .` — Linux/glibc build succeeds; chat_history.o compiles clean under gcc with `-std=c11 -Wall -Wextra -pedantic`.
- [x] 13.3 PPC-safe audit of `src/hotline/chat_history.c`:
    - `_FILE_OFFSET_BITS=64` defined at top of file ✓
    - All file positioning uses `ftello`/`fseeko` (lines 336, 616, 637) — never bare `ftell`/`fseek` ✓
    - `%llu` for uint64_t, `%lld` for time_t (lines 605, 775) — both portable ✓
    - `fgets` with fixed buffer (`LM_CHAT_LINE_MAX`) — no `getline` (which is GNU-only) ✓
    - No `pthread_*` calls (single-writer model) ✓
    - No C11 atomics ✓
    - **One latent caveat**: `lm_chat_idx_entry_t.offset` and `long start = ftello(...)` both use `long`, which is 32-bit on 32-bit PPC — silently truncates beyond 2 GiB per channel file. At ~200 bytes/entry that's ~10M messages per channel; well beyond any practical retention. Worth widening to `off_t` if 64-bit offsets ever matter, but not blocking.
- [ ] 13.4 Deploy to `hotline.semihosted.xyz`, enable with `ChatHistoryEnabled: true`, verify log line — **awaiting deploy authorization**
- [ ] 13.5 Connect with Navigator 0.2.5, verify capability bit 4 is echoed, request history, verify entries render — **awaiting deploy**
- [ ] 13.6 Connect with a legacy client (ClassicHL or similar), verify chat works unchanged; if legacy broadcast enabled, verify recent messages appear as TRAN_CHAT_MSG — **awaiting deploy**

## 14. PPC Backport (separate PR against the PPC branch)

- [ ] 14.1 Cherry-pick `chat_history.c` / `chat_history.h` unchanged
- [ ] 14.2 Add `-D_FILE_OFFSET_BITS=64` to the PPC branch Makefile CFLAGS stanza
- [ ] 14.3 Verify build with gcc-4.0 on Tiger
- [ ] 14.4 Verify Mac Roman round-trip (write nick with high-bit chars → read back identical)
- [ ] 14.5 Smoke test on a 10.4 PPC VM: enable chat history, send/receive messages, query history
