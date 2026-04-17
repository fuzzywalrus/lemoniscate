## Context

Lemoniscate is a C-based Hotline server that currently runs only on macOS. A code audit identified 5 macOS-specific touch points in the server binary:

| File | macOS API | Lines | Linux replacement |
|------|-----------|-------|-------------------|
| `server.c` | kqueue | ~200 | epoll + timerfd |
| `tls.c` | SecureTransport | 628 | OpenSSL |
| `hope.c` + `password.c` | CommonCrypto | ~370 | OpenSSL libcrypto |
| `encoding.c` | CoreFoundation | 86 | 256-byte lookup table |
| `bonjour.c` | dns_sd | 63 | compile out |
| `config_plist.c` | CoreFoundation | 165 | compile out |

The remaining ~12,000 lines of C code (protocol, transactions, file operations, YAML config, account management) are already pure C/POSIX and compile on Linux without changes.

The project must continue to support three macOS targets: modern macOS (10.11+), Mac OS X 10.4 Tiger (PPC, gcc-4.0), and the GUI app bundle.

## Goals / Non-Goals

**Goals:**
- Run the Lemoniscate CLI server binary on Linux x86_64 with full protocol support
- Provide a Dockerfile for containerized deployment
- Maintain 100% feature parity between macOS and Linux (except Bonjour and plist config, which are macOS-only features)
- Keep all three macOS build targets working without regression
- Use compile-time platform selection, not runtime detection

**Non-Goals:**
- Porting the GUI (`lemoniscate-gui`) or the Obj-C client library (`client.m`) to Linux
- Supporting BSDs, Windows, or other Unix variants (though the abstraction shouldn't prevent it)
- Runtime platform detection or dynamic dispatch — this is a compile-time split
- Avahi/mDNS support on Linux (can be added later as a separate change)
- Supporting musl libc (target glibc first, musl compatibility can follow)

## Decisions

### 1. Header-based platform abstraction with per-platform source files

**Decision:** Create platform abstraction headers (`platform_event.h`, `platform_tls.h`, `platform_crypto.h`) that define the abstract API. Each header `#include`s the correct platform implementation via `#ifdef`. Platform-specific implementations live in separate `.c` files (`event_kqueue.c`, `event_epoll.c`, `tls_sectransport.c`, `tls_openssl.c`, etc.). The Makefile selects which `.c` files to compile based on `uname -s`.

**Rationale:** Separate source files keep the platform code clean and independently maintainable. Headers define the contract — implementation files fulfill it. The Makefile already uses conditional flags for Tiger vs modern; extending this to `Darwin` vs `Linux` is natural.

**Alternatives considered:**
- `#ifdef` blocks within existing files: Messier, harder to read, but fewer files. Rejected because `tls.c` is already 628 lines and splitting by `#ifdef` within that file would be painful.
- Runtime dispatch via function pointers: Unnecessary complexity — we always know the platform at compile time.
- Autoconf/CMake: Overkill for 2 platforms with known, fixed dependencies. The existing Makefile is simple and clear.

### 2. epoll + timerfd for the Linux event loop

**Decision:** Use `epoll_create1`, `epoll_ctl`, `epoll_wait` for I/O multiplexing and `timerfd_create` for timers. The abstracted API mirrors kqueue's semantics: register interest in fd events, poll with timeout, iterate results.

**Rationale:** epoll is the standard Linux event mechanism. It maps cleanly to kqueue:

```
kqueue()           → epoll_create1(EPOLL_CLOEXEC)
EV_SET + kevent    → epoll_ctl(EPOLL_CTL_ADD)
kevent(poll)       → epoll_wait()
EVFILT_READ        → EPOLLIN
EVFILT_TIMER       → timerfd_create + EPOLLIN on the timerfd
```

The main difference is timers: kqueue has `EVFILT_TIMER` built in, while epoll needs a separate `timerfd` that gets polled like any other fd. This is a well-known pattern.

**Alternatives considered:**
- `poll()`: POSIX portable but O(n) per call. Rejected for servers with many connections.
- `select()`: Even worse scalability, 1024 fd limit on most systems.
- `io_uring`: Modern and fast, but requires kernel 5.1+ and more complex setup. Can be added later as a high-performance option.
- `libevent`/`libuv`: External dependencies, overkill for direct epoll usage.

### 3. OpenSSL for TLS on Linux

**Decision:** Use OpenSSL (`libssl`, `libcrypto`) for both TLS connections and cryptographic primitives (SHA-1, MD5, HMAC) on Linux. `tls.c` already has `#ifdef __APPLE__` with non-functional stubs in the `#else` block — the OpenSSL implementation fills those stubs.

**Rationale:** OpenSSL is the standard TLS library on Linux, available everywhere, and PEM cert/key loading is trivial compared to SecureTransport's keychain dance. The crypto primitives (`SHA1_Init`/`MD5_Init`) are nearly 1:1 with CommonCrypto.

**Alternatives considered:**
- GnuTLS: Viable but less commonly installed. Some distros prefer it, but OpenSSL is more universal.
- wolfSSL: Lightweight, embeddable, but less common in package managers.
- Separate crypto library (libsodium): SHA-1/MD5 are legacy algorithms that libsodium intentionally doesn't support. OpenSSL has them.

### 4. Static lookup table for MacRoman ↔ UTF-8 encoding

**Decision:** Replace the CoreFoundation-based encoding conversion with a 256-entry lookup table mapping MacRoman byte values to UTF-8 sequences. The first 128 entries are identity (ASCII). The upper 128 entries map to their UTF-8 multi-byte equivalents.

**Rationale:** MacRoman is a fixed single-byte encoding. The entire mapping fits in a static array. No library dependency needed. This is the approach used by most non-Apple Hotline implementations.

**Alternatives considered:**
- iconv: Available on Linux but adds a runtime dependency for a trivial conversion.
- ICU: Massive library, absurd overkill for one 256-byte encoding.

### 5. Compile out macOS-only features on Linux

**Decision:** Bonjour (`bonjour.c`) and plist config (`config_plist.c`) are wrapped in `#ifdef __APPLE__` and compiled out on Linux. No stub implementations — the calling code checks availability before calling these features.

**Rationale:** Bonjour is a LAN discovery feature irrelevant to headless/Docker deployment. Plist is a macOS preference system — YAML is the universal config path. Both are cleanly optional today (config flags control them), so compile-gating adds no behavior change.

### 6. Debian-based Docker image

**Decision:** Provide a Dockerfile based on `debian:bookworm-slim` with a multi-stage build (build stage with dev packages, runtime stage with only shared libraries and the binary).

**Rationale:** Debian is the most common base image for server containers. Multi-stage build keeps the runtime image small (~30-50 MB). The server binary, config directory, and file root are the only runtime requirements.

## Risks / Trade-offs

- **[OpenSSL version differences]** → OpenSSL 1.1 vs 3.x have API differences (e.g., `SSL_CTX_new` signature, deprecated low-level crypto APIs). Mitigation: target OpenSSL 3.x (ships with Debian 12+/Ubuntu 22.04+), use `EVP_*` APIs for crypto which work across versions.

- **[epoll edge-triggered vs level-triggered]** → kqueue defaults to level-triggered; epoll supports both. Mitigation: use level-triggered (`EPOLLIN` without `EPOLLET`) to match kqueue behavior exactly. Edge-triggered optimization can be added later.

- **[Three build targets to maintain]** → macOS modern, macOS Tiger, and Linux. Mitigation: the platform abstraction isolates changes. Protocol/handler code is shared and doesn't touch platform APIs. CI should test all three.

- **[Timer semantics mismatch]** → kqueue `EVFILT_TIMER` is implicit; epoll timers need explicit timerfd management. Mitigation: the platform event API abstracts timer registration/cancellation — callers don't see the implementation difference.

- **[SHA-1 deprecation in OpenSSL 3.x]** → Low-level `SHA1_Init` etc. are deprecated in favor of `EVP_DigestInit`. Mitigation: use `EVP_*` APIs from the start for Linux. CommonCrypto APIs on macOS are not deprecated.

## Migration Plan

1. Create platform headers and macOS backends (refactor existing code, no behavior change)
2. Write Linux backends (epoll, OpenSSL, lookup table)
3. Update Makefile with Linux detection and flags
4. Cross-compile or build on a Linux VM/container to validate
5. Create Dockerfile and test containerized deployment
6. Test full protocol compatibility (connect from Hotline clients, file transfers, TLS, HOPE, trackers)

## Open Questions

- **CI strategy** — GitHub Actions can build Linux natively. macOS modern builds on macOS runners. Tiger PPC cross-compilation would need a special environment or manual testing. How should CI be structured?
- **Static vs dynamic linking on Linux** — Static linking produces a single binary with no runtime dependencies (simpler Docker image). Dynamic linking is smaller and uses system OpenSSL (security updates). Which is preferred?
- **Alpine/musl support** — Should the initial Linux target also support Alpine (musl libc)? musl has some differences in `getaddrinfo`, signal handling, and thread-local storage. Recommend deferring to a follow-up.
