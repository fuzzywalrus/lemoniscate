## Context

The chat-history proposal defines the wire protocol but deliberately leaves storage implementation-defined. This design captures the server-side choices for Lemoniscate's implementation. The constraints that shape these choices:

- **Tiger PPC compatibility.** Lemoniscate's PPC port targets Mac OS X 10.4 / gcc-4.0 / C99. Storage code should be source-identical between modern and PPC branches.
- **No new dependencies.** The project ships with libssl + libyaml only. Adding SQLite or a third-party DB breaks the Tiger build and the zero-dep posture cemented in 0.1.6.
- **SSHFS production mounts.** At least one deployment (Apple Media Archive) keeps its config on an SSHFS mount from a Raspberry Pi. Random I/O is slow; append-only sequential writes are fine.
- **ChaCha20-Poly1305 already available.** 0.1.6 shipped a portable C implementation for HOPE AEAD. It is Tiger-compatible and can be reused for encryption at rest without adding a dep.
- **Existing persistence patterns.** `MessageBoard.jsonl` (JSONL append) and `News/` (directory-per-bucket) landed in 0.1.6. Chat history should fit the same idiom.
- **Expected scale.** Target deployments are social Hotline servers with 10s of active users and ≤100k retained messages per channel.

## Goals / Non-Goals

**Goals:**
- Persist public chat messages with server-assigned monotonic IDs.
- Serve cursor-based pagination (latest / BEFORE / AFTER / range) in O(log n) per query.
- Fit the existing JSONL + vtable convention used by `MessageBoard.jsonl`.
- Source-identical storage module between modern and PPC branches.
- Optional encryption at rest for message bodies using the existing ChaCha20-Poly1305 code.
- Self-healing on crash: no separate WAL, no external recovery tool.

**Non-Goals:**
- Named channels (1+). Channel 0 only for v1; the file layout is per-channel to avoid a migration later.
- Full-text search. Mnemosyne is the search layer; chat history feeds it, doesn't index locally.
- Multi-writer safety. Single-process server.
- Per-message durability. `fsync` is periodic, not per-append. Power loss may lose the last minute of chat — acceptable for this domain.

## Decisions

### 1. JSONL, one file per channel

**Decision:** Store chat messages in `ChatHistory/channel-<N>.jsonl`, one channel per file, newline-delimited JSON.

**Rationale:** Mirrors `MessageBoard.jsonl`. Per-channel sharding means named channels (future) require no data migration; just a new filename. Channel deletion is `rm`. Retention can be per-channel. Mnemosyne sync of chat is identical in shape to the existing msgboard sync.

**Layout:**
```
ChatHistory/
├─ channel-0.jsonl          public chat (created when chat history enabled)
├─ channel-1.jsonl          reserved — created lazily when named channels land
└─ channel-N.jsonl
```

**Line format (plaintext mode):**
```json
{"id":1000,"ch":0,"ts":1729137536,"flags":0,"icon":128,"nick":"greg","body":"Hello everyone"}
```

Fields are decimal integers or JSON-escaped UTF-8 strings. The `ch` field is redundant with the filename but included for Mnemosyne sync payloads and for forward compatibility.

### 2. Global monotonic message IDs

**Decision:** A single server-wide `uint64` counter assigns IDs across all channels. Seeded on startup from `max(existing ids) + 1`; incremented in memory on each append.

**Rationale:** The spec requires monotonic IDs but does not scope the sequence. A global counter keeps the index simple (`uint64` → offset, no composite keys), prevents cursor confusion if a client mixes channels, and leaves room for future cross-channel features (reply-to, reactions).

**Seeding:** During the startup index scan, track the maximum ID seen across all channel files. `next_id = max + 1`. If no files exist, start at 1.

### 3. In-memory index per channel

**Decision:** Each channel maintains an in-memory array of `{id, offset, length}`, sorted by `id` (which is also chronological given monotonic IDs).

**Rationale:** Enables O(log n) cursor lookup by binary search on `id`. At 16 bytes per entry, 100k messages = 1.6 MB — trivial. Built by scanning the JSONL file on startup extracting only the `"id":` substring (no full JSON parse). Appended to in memory on each write.

```c
typedef struct {
    uint64_t id;
    long     offset;    /* ftello() into channel-N.jsonl */
    uint32_t length;    /* line length in bytes */
} lm_chat_idx_entry_t;
```

Cursor query: binary-search for the cursor ID, then walk forward/backward `limit` entries, issuing `pread` for each line. Lines are short (hundreds of bytes), so read cost is dominated by syscall overhead.

### 4. Body-only encryption at rest (optional)

**Decision:** When `ChatHistoryEncryptionKey` is configured, encrypt only the `body` field. All other fields stay plaintext. Uses the portable ChaCha20-Poly1305 implementation shipped in 0.1.6.

**Rationale:**
- Keeps the startup index scan fast (only needs `id`, which is plaintext).
- Lets `tail -f` and `grep` remain useful for operations.
- Nicks are not secret — they are broadcast on the wire to everyone in the room.
- Message content is the actual sensitive surface. Matches Signal/Matrix at-rest designs.

**Format:**
```json
{"id":1000,"ch":0,"ts":...,"nick":"greg","body":"ENC:<base64(nonce‖ciphertext‖tag)>"}
```

- Key: 32 bytes, raw, from file path configured as `ChatHistoryEncryptionKey`.
- Nonce: 12 bytes, random per line (secure RNG).
- AAD: the string `"chat-history-v1\0"` — domain separator, fixed.
- Encoding: base64 with `ENC:` prefix so a mixed-mode file (pre/post encryption enable) remains parseable.

**Key rotation:** Out of scope for v1. When needed, add a `"kv":N` key-version field and keep old keys in a keyring. Lines without `"kv"` use the default key.

### 5. Hourly retention prune off existing timer loop

**Decision:** A 60-minute timer iterates each channel, rewrites its JSONL file dropping lines past the `MaxMessages` count or `MaxDays` age. Also runs once on startup.

**Rationale:** At expected scale (hundreds of messages/day), once-per-hour is more than sufficient. Mnemosyne's timer subsystem (5-minute heartbeat + 15-minute periodic) is already integrated in the server event loop — chat prune slots in as another tick.

**Thresholds:**
```
prune triggers when:
  count > (MaxMessages + slack)       slack = 10% of MaxMessages
  OR oldest.ts < (now - MaxDays·86400)
```

The slack band prevents thrash at the count boundary (one msg crosses 10k → prune → another msg crosses → prune again).

**Rewrite strategy:** Write a fresh `channel-<N>.jsonl.tmp` with surviving lines, `fsync`, then `rename` over the original. Atomic replace. Index is rebuilt from the new file in the same pass.

At 10k messages × ~200 bytes = ~2 MB, the rewrite takes well under a second even on SSHFS.

### 6. Rate limiting: token bucket per connection

**Decision:** Each `client_conn_t` carries a token bucket for `TRAN_GET_CHAT_HISTORY`: capacity 20 tokens, refill 10 tokens/sec.

**Rationale:** Strict 10/sec windows reject legitimate scroll-back bursts (5 requests in 200ms is normal UI behavior). A token bucket absorbs short bursts while throttling sustained abuse.

```c
typedef struct {
    uint16_t tokens;          /* current bucket level, × 10 for 0.1-token resolution */
    uint64_t last_refill_ms;  /* monotonic ms of last refill */
} lm_chat_rl_t;
```

- Default capacity: 20 tokens.
- Default refill: 10 tokens/sec (≈ spec recommendation).
- Config keys: `ChatHistoryRateCapacity`, `ChatHistoryRateRefillPerSec`.
- On empty bucket: reply with a standard Hotline error transaction, descriptive text `"chat history rate limited"`. Does not drop silently (leaves clients hanging).
- Per-connection only; not per-IP. Eviction comes for free with connection close.

### 7. Crash safety: validate-on-startup with truncate

**Decision:** On startup, each channel file is scanned line by line. The offset after the last successfully parsed line is recorded; if the file ends past that offset, `ftruncate` back to it.

**Rationale:** Zero runtime overhead. No WAL, no tempfile-per-write. `O_APPEND` (via `fopen "a"`) provides kernel-atomic append for lines ≤ PIPE_BUF (typically 4 KB; chat messages fit). Partial lines from power loss or `kill -9` are detected and trimmed on startup.

```c
on channel file open:
    off_t last_good = 0;
    while (fgets(line, sizeof(line), f))
        if (parse_line_id(line, &id) == 0) last_good = ftello(f);
        else break;
    if (ftello(f) != last_good) ftruncate(fd, last_good);
```

**Write path:**
```
on chat_send (public channel):
    format line in stack buffer
    single fwrite()
    fflush()
    (no per-line fsync)
append line to in-memory index
```

**Periodic fsync:** Once per 60 seconds and as part of the hourly prune. Bounds worst-case loss on power failure to ~1 minute of chat. Acceptable for this domain.

### 8. PPC backport surface

**Decision:** The storage module is a single C file + header, written in C99, using only standard POSIX I/O. It compiles unchanged on both branches.

**PPC-specific requirements (documented for the port):**

| Concern | Requirement |
|---|---|
| Large files (>2 GB) | Makefile adds `-D_FILE_OFFSET_BITS=64`; use `ftello`/`fseeko`/`off_t` |
| uint64 formatting | `printf("%llu", ...)`, `strtoull` — avoid `%ju` (C99 standard, PPC toolchains flaky) |
| Line reading | `fgets` with 4096-byte stack buffer — do not use `getline` (GNU extension on Tiger) |
| Atomic rename | `rename(2)` — POSIX, works on HFS+ and SSHFS |
| Random nonces | `SecRandomCopyBytes` on Darwin (both), `/dev/urandom` on Linux — wrap behind existing `hl_crypto_rand_bytes` |
| Mac Roman bodies | Transcode to UTF-8 before storage; transcode back on emit (matches live `TRAN_CHAT_MSG` behavior) — reuses existing encoding layer |

No pthreads, no atomics, no C11 features, no mmap required.

### 9. Vtable interface

**Decision:** Follow the existing `mobius_flat_news_t` / `mobius_threaded_news_t` pattern. Define `lm_chat_history_t` with function-pointer vtable: `append`, `query`, `tombstone`, `prune`, `close`.

**Rationale:** Keeps the transaction handler ignorant of the backend. Enables future swaps (e.g., ring buffer for ephemeral mode, remote backend for clustered setups) without touching handler code. Matches the project's existing separation between protocol and storage.

## Risks / Trade-offs

- **Startup index scan is O(n).** At 100k messages ≈ 200 ms on SSHFS. If deployments grow past 1M messages per channel, a sidecar index file (`channel-0.idx`) becomes worthwhile. Deferred until observed.
- **No per-message fsync = ~1 min loss window on power failure.** Explicit trade for write throughput on SSHFS. Operators needing stronger durability can enable `ChatHistoryFsyncPerWrite` (not in v1 scope).
- **Body-only encryption leaks metadata.** An attacker with file access sees who chatted when, just not what. Whole-file or whole-line encryption would hide this at the cost of debuggability and the ability to index without decrypt. If metadata secrecy matters for a deployment, filesystem-level encryption (LUKS, APFS encryption) is the right layer.
- **Global ID counter is process-local.** A cluster of Lemoniscate servers sharing storage would collide. Out of scope — single-process is the project's model.
- **Rewrite-based pruning creates temporary disk pressure.** At 2 MB per file, negligible. At 200 MB (past our scale target), would need streaming rewrite or sliding-window deletion.

## Migration

There is nothing to migrate. Chat history is new behavior behind a feature flag (`ChatHistoryEnabled`). Servers with it disabled behave exactly as 0.1.6 does today. Enabling it creates `ChatHistory/channel-0.jsonl` on first message and backfills nothing.

## Open Questions

- Should admin tombstone operations (bit 2 `is_deleted`) rewrite the line in place with padding, or append to a sidecar `tombstones.jsonl`? Sidecar is simpler and easier to audit; in-place keeps the file self-describing. Leaning sidecar.
- Should `ChatHistoryEnabled: false` retain the file if it exists, or archive it? Lean retain untouched.
- Legacy broadcast (spec allows sending recent messages to bit-4-less clients as `TRAN_CHAT_MSG` on connect) — off by default? On with 30-message default like msgboard? Leaning off-by-default to avoid surprising operators whose users see history unexpectedly.
