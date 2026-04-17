## Context

The `modern` branch (at `/Users/greggant/Development/lemoniscate-local`) has diverged significantly from PPC `main` since v0.1.4. The modern branch targets x86 macOS (and now Linux via Docker), while this repo targets PowerPC Mac OS X Tiger 10.4 with GCC 4.0.

The modern branch introduced a platform abstraction layer to support Linux alongside macOS. For PPC, we only need the macOS implementations (SecTransport, CommonCrypto, kqueue, CoreFoundation encoding) but must adopt the new header structure so the codebase stays in sync.

The modern branch also went through a critical architectural lesson: an early attempt to replace client-facing news/message board backends with JSONL/directory formats broke the Hotline wire protocol. The corrected approach uses those formats as sync-only readers for Mnemosyne. This port must follow the corrected approach.

Key constraints:
- **GCC 4.0**: No C11 features, limited C99. No `_Generic`, no `__builtin_expect` beyond basic usage.
- **Tiger 10.4 SDK**: Limited Cocoa APIs. No blocks, no GCD, no `NSViewController`.
- **No OpenSSL on PPC build**: TLS uses SecTransport only.
- **No epoll**: kqueue only for event handling.
- **32-bit PowerPC**: Pointer size, alignment, and endianness differences from x86_64.

## Goals / Non-Goals

**Goals:**
- Port all non-Linux-specific functionality from modern branch to PPC
- Adopt the platform abstraction headers so future syncs are easier
- Bring Mnemosyne search integration, sync-only storage readers, and GUI updates to PPC
- Maintain buildability with GCC 4.0 on Tiger 10.4
- Keep PPC Makefile aligned with modern branch structure
- Preserve all existing client-facing backend behavior exactly

**Non-Goals:**
- Linux/Docker support (epoll, OpenSSL backends) — not applicable to PPC
- Tracker V3 registration — not yet functional on modern branch
- Upgrading to a newer compiler or SDK
- Refactoring existing working PPC code beyond what's needed for the port
- Performance optimization beyond what the modern branch already provides
- Replacing any client-facing storage backends

## Decisions

### 1. Cherry-pick by subsystem, not by commit

**Decision**: Port changes subsystem-by-subsystem in 6 phases (platform → Mnemosyne core → sync readers → YAML fixes → GUI → tests/docs) rather than replaying individual commits.

**Rationale**: The modern branch has merge commits, reverts, and duplicate commit messages (three "0.1.5" commits). A commit-by-commit cherry-pick would be fragile. Subsystem porting lets us adapt each piece to PPC constraints as we go. The 6-phase order matches the proven build sequence from the modern branch.

**Alternative considered**: `git cherry-pick` range — rejected due to merge commits and platform-specific code that would need immediate modification.

### 2. Platform abstraction: macOS-only backends

**Decision**: Copy all `platform/*.h` headers as-is, but only bring `crypto_commoncrypto.c`, `encoding_cf.c`, `event_kqueue.c`, and `tls_sectransport.c` implementations. Skip `crypto_openssl.c`, `encoding_table.c`, `event_epoll.c`, `tls_openssl.c`.

**Rationale**: PPC only runs macOS. The headers define the interface; the Makefile selects which `.c` files to compile. This keeps the API surface identical to modern branch.

**Alternative considered**: Ifdef-based single files — rejected because the modern branch already split them, and diverging the structure creates merge pain later.

### 3. Sync-only storage readers — DO NOT replace client backends

**Decision**: Port `dir_threaded_news.c` and `jsonl_message_board.c` as **read-only modules consumed exclusively by the Mnemosyne sync subsystem**. They MUST NOT be wired into any transaction handler or client-facing code path. The existing `flat_news.c` and `threaded_news_yaml.c` remain the sole backends for Hotline client operations.

**Rationale**: The modern branch's archived `janus-news-storage` change attempted to replace client-facing backends with JSONL/directory formats and **broke the Hotline wire protocol** — clients saw empty message bodies and missing articles. The corrected approach (`janus-sync-reader`) keeps these as sync-only parsers. The pattern is:

```
Hotline clients ──→ flat_news.c / threaded_news_yaml.c ──→ Wire format ✓
                    (UNCHANGED — these produce correct wire bytes)

Mnemosyne sync ←── sync readers ←── parse same files at sync time ✓
                    (read-only, never writes, never serves clients)
```

**Alternative considered**: Replacing backends (the archived approach) — rejected because it broke the wire protocol.

### 4. Mnemosyne sync: full protocol complexity

**Decision**: Port the complete `mnemosyne_sync.c` with all its subsystems: chunked full sync (state machine with persistence), incremental sync (64-entry ring buffer with event hooks), drift detection (15-minute periodic checks), exponential backoff and sync suspension, DNS caching, aggressive timeouts (2s connect, 5s total), API key via query parameter.

**Rationale**: The modern branch's Mnemosyne implementation (91 tasks in the original change) is a mature, battle-tested system. Cherry-picking subsets would leave broken state machine transitions. Port it whole.

### 5. HTTP client: direct socket implementation

**Decision**: Port `http_client.c` as-is. It uses raw sockets (no libcurl dependency), which works fine on PPC Tiger.

**Rationale**: The modern branch's HTTP client is already minimal and dependency-free. No adaptation needed beyond endianness audit.

### 6. GUI changes: incremental merge with Tiger API audit

**Decision**: Port the GUI `.inc` and `.m` files but audit each Cocoa API call against the Tiger 10.4 SDK headers. Replace any post-Tiger APIs with Tiger-compatible equivalents. Default encoding to Mac Roman (preserving classic Hotline client compatibility).

**Rationale**: The modern branch's GUI was developed on a newer macOS. Some APIs (like `NSStackView`, newer `NSAlert` patterns) may not exist on Tiger. Each must be verified.

### 7. Endianness handling

**Decision**: Audit all network byte-order operations and struct packing in ported code. The modern branch runs on little-endian x86; PPC is big-endian. The existing PPC codebase already handles this, but new code (especially Mnemosyne HTTP parsing, JSON builder) must be verified.

**Rationale**: Hotline protocol is big-endian on the wire, which is native for PPC but required byte-swapping on x86. New code that was only tested on x86 may have implicit little-endian assumptions.

## Risks / Trade-offs

- **Accidentally wiring sync readers as client backends** → Mitigation: Explicit verification task after porting. Code review that `jsonl_message_board.c` and `dir_threaded_news.c` are only `#include`d by Mnemosyne sync code. No transaction handler references.
- **Tiger Cocoa API gaps** → Mitigation: Audit each GUI file against 10.4 SDK before compiling. Stub or rewrite unavailable APIs with Tiger equivalents.
- **GCC 4.0 C99 edge cases** → Mitigation: Compile early and often on the PPC build machine. Fix syntax issues as they arise (designated initializers, variable-length arrays, mixed declarations).
- **Endianness bugs in new code** → Mitigation: Review all `htons`/`ntohs`/`htonl`/`ntohl` usage in new files. Add byte-swap where needed for HTTP response parsing and JSON output.
- **Large diff size (~11k lines)** → Mitigation: Port in 6 phases with build verification after each phase.
- **Mnemosyne live tests require network** → Mitigation: `test_mnemosyne_live.c` is optional; basic `test_mnemosyne.c` should work offline.

## Open Questions

- Does the PPC G4 build machine have network access to test Mnemosyne HTTP integration?
- Are there any Tiger-specific `kqueue` limitations that differ from the modern branch's usage?
- Should the version bump to 0.1.5 happen on PPC, or should PPC use a separate version scheme (e.g., 0.1.5-ppc)?
