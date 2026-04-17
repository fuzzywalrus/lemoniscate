# Backporting Chat History to the PPC (Tiger) Branch

This guide covers cherry-picking the chat history extension from
`modern` (0.1.7) to the PPC branch targeting Mac OS X 10.4 PPC.

The change was designed PPC-safe from day one — `_FILE_OFFSET_BITS=64`,
`ftello`/`fseeko`, `%llu`/`%lld` format specifiers, `fgets` (not GNU
`getline`), and no pthreads or C11 atomics. The only Tiger gotchas are
build flags and a few Foundation idioms in the GUI patch (the GUI
runs on macOS 10.11+ on `modern`; on Tiger you only need the server +
CLI portion).

## What to cherry-pick

### Required commits (in order)

1. **`f3a3c51`** — *Persistent chat history (extension), 0.1.7 release, build/GUI fixes*
   - Storage layer, transaction handler, capability negotiation, rate
     limiter, prune scheduler, encryption-at-rest, Makefile header-deps fix
2. **`167332b`** — *Chat history: optional sign-on / sign-off events*
   - `ChatHistoryLogJoins` flag + hook in `disconnect_client` and login

```bash
git checkout ppc
git cherry-pick f3a3c51 167332b
```

### What you do NOT need on Tiger

- `src/gui/*` — Tiger has no Cocoa Preferences panel; CLI/YAML/plist work
  fully. Skip the conflicts here with `git checkout --ours src/gui`.
- `resources/Info.plist` version bump if PPC ships under its own
  versioning scheme.

### Files added (no conflicts expected)

- `include/hotline/chat_history.h`
- `src/hotline/chat_history.c`
- `test/test_chat_history.c`
- `openspec/changes/chat-history/{proposal,design,tasks}.md`

## Tiger-specific build adjustments

### Makefile

The PPC Makefile already has a Tiger-specific stanza (commented out on
`modern`). Add `-MMD -MP` to its CFLAGS so header-dep tracking works:

```make
# Tiger-specific flags
CC = gcc-4.0
CFLAGS = -std=c99 -Wall -Wextra -pedantic -O2 \
         -mmacosx-version-min=10.4 \
         -I./include -I/usr/local/include \
         -DTARGET_OS_MAC=1 \
         -D_FILE_OFFSET_BITS=64 \
         -MMD -MP                            # ← add
```

`_FILE_OFFSET_BITS=64` is set inside `chat_history.c` already, but
defining it project-wide on Tiger is safer (prevents off_t mismatches
between translation units).

The existing `-include $(ALL_OBJS:.o=.d)` block from cherry-picked
`Makefile` works on gcc-4.0 — `-MMD` was added in gcc 3.x.

### Add chat_history.c to the source list

Already in the modern Makefile:

```make
HOTLINE_C_SRCS = \
    src/hotline/field.c \
    ...
    src/hotline/chat_history.c \                # ← ensure this is in PPC Makefile
    ...
```

## Verification on Tiger

### 1. Build clean

```bash
make clean
make
```

Expect zero warnings under gcc-4.0 + `-Wall -Wextra -pedantic -std=c99`.
The chat history code was tested cleanly under modern clang's
`-pedantic`; gcc-4.0 is pickier about C99-strict idioms but uses none
that this code violates.

### 2. Mac Roman round-trip test

The 700 reply transcodes stored UTF-8 → wire encoding. On Tiger with
PPC clients, this is MacRoman. Verify:

```bash
# Server with chat history enabled, Mac Roman wire encoding
./lemoniscate -c ./config -p 5500 -l info

# From a Tiger Hotline client, send a chat with high-bit chars
# (e.g. "Caf\xE9" using Mac Roman é = 0x8E)

# Stop server, inspect storage:
cat config/Files/ChatHistory/channel-0.jsonl
# Body should contain UTF-8 "Café" → bytes "Caf\xC3\xA9"
```

### 3. Test suite

`test/test_chat_history.c` is portable:

```bash
make test-chat-history
# Expect: 11/11 tests passed
```

`test_corrupt_truncate` and `test_prune_by_age` write directly to JSONL
files. These work identically on Tiger.

### 4. Smoke test on a 10.4 PPC VM

- Enable chat history (config: `ChatHistoryEnabled: true`)
- Send public chat from a classic Hotline 1.x client
- Verify message lands in `ChatHistory/channel-0.jsonl`
- Verify private chat (FieldChatID present) does NOT appear
- Disconnect and reconnect with `LegacyBroadcast: true` → recent messages
  replayed as standard `TranChatMsg` (106)

## Known PPC caveats

### `lm_chat_idx_entry_t.offset` is `long`, not `off_t`

Both `lm_chat_idx_entry_t.offset` and the local `long start = ftello(...)`
in `lm_channel_scan` use `long`. On 32-bit PPC `long` is 32 bits, which
silently truncates beyond 2 GiB per channel file (~10M messages at
~200 B/entry). Far beyond any realistic retention; flagged here for
completeness. Widening to `off_t` would close the gap if it ever
matters.

### libyaml on Tiger

PPC builds may need to bring their own libyaml; the modern branch uses
homebrew. Same as the rest of the project — no chat-history-specific
issue here.

### No GUI Preferences surface

The chat history Preferences panel section lives in
`src/gui/AppController+LayoutAndTabs.inc` and only builds on macOS
10.11+ (modern Cocoa idioms like `NSButtonTypeSwitch`). Tiger users
configure via `config.yaml` directly:

```yaml
ChatHistory:
  Enabled: true
  MaxMessages: 10000
  MaxDays: 30
  LegacyBroadcast: true
  LegacyCount: 30
  LogJoins: false
```

Or via `~/Library/Preferences/com.lemoniscate.server.plist` flat keys
(`ChatHistoryEnabled`, etc.) if running through the (Tiger-compatible)
GUI launcher.

## Optional: cherry-pick the Makefile header-deps fix separately

If you want to land the `-MMD -MP` improvement on PPC ahead of chat
history (it prevents the silent partial-rebuild → struct mismatch
segfault that bit us), the relevant hunk is small and self-contained:

```bash
git show f3a3c51 -- Makefile | git apply
```

Then test by touching a header and confirming dependent `.o` files
rebuild.

## Rollback

The change is additive. To disable at runtime: set
`ChatHistoryEnabled: false` (or remove the key) and restart. No
JSONL files are read or written. `ChatHistory/` directory and any
existing files are left untouched on disk.

To roll back the code: `git revert 167332b f3a3c51` (in reverse).
The schema (JSONL) is forward-compatible, so re-applying later picks
up where you left off.
