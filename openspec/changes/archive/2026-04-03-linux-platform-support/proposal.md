## Why

Lemoniscate is a fully functional Hotline server with protocol support beyond 1.9 — HOPE encryption, TLS, threaded news, full file transfer with resume, 43 transaction handlers, and tracker registration. It's ready to ship, but it only runs on macOS. Adding Linux support enables headless deployment on commodity servers and Docker containers, which is where most people would actually run a Hotline server today. The macOS-specific surface is concentrated in 5-6 files — the vast majority of the codebase is already pure C and POSIX.

## What Changes

- Introduce a platform abstraction layer with headers (`platform_event.h`, `platform_tls.h`, `platform_crypto.h`, `platform_encoding.h`) and per-platform backends behind compile-time selection.
- **Event loop**: Abstract kqueue behind `platform_event.h`. macOS backend uses kqueue, Linux backend uses epoll + timerfd.
- **TLS**: Replace SecureTransport with an OpenSSL backend on Linux. `tls.c` already has `#ifdef __APPLE__` with stubs for non-Apple — fill in the `#else` with OpenSSL (`SSL_CTX`, `SSL_read`, `SSL_write`).
- **Cryptography**: Replace CommonCrypto (SHA-1, MD5, HMAC) with OpenSSL libcrypto on Linux. The APIs are nearly 1:1 (`CC_SHA1_Init` → `SHA1_Init`).
- **Text encoding**: Replace CoreFoundation string conversion (MacRoman ↔ UTF-8) with a 256-byte lookup table. MacRoman is a fixed single-byte encoding — no library needed.
- **Bonjour**: Compile out on Linux via `#ifdef`. Bonjour is a LAN discovery feature not relevant to headless/Docker deployment. Avahi support can be added later if needed.
- **Plist config**: Compile out on Linux via `#ifdef`. YAML is the primary configuration path and works everywhere via libyaml.
- **Makefile**: Add a Linux target with `epoll`, `libssl`, `libcrypto`, and `libyaml` linkage. Remove macOS framework flags on Linux.
- **Dockerfile**: Add a minimal Dockerfile for containerized deployment.
- **Fix TLS port quirk**: The v1 tracker registration currently sends TLS port in the "reserved" field. This should be `0x0000` for strict v1 compliance on all platforms.

## Capabilities

### New Capabilities
- `platform-abstraction`: Platform abstraction layer covering event loop (kqueue/epoll), TLS (SecureTransport/OpenSSL), cryptographic primitives (CommonCrypto/libcrypto), and text encoding (CoreFoundation/lookup table). Defines the compile-time selection mechanism and the abstracted APIs that the rest of the codebase uses.
- `docker-deployment`: Dockerfile and container configuration for running Lemoniscate as a headless Linux service. Covers image build, volume mounts for config/files, port exposure, and signal handling for graceful shutdown.

### Modified Capabilities
- `networking`: Event loop abstraction changes the kqueue-specific API calls in `server.c` to platform-agnostic wrappers. Behavioral requirements (accept connections, handle I/O, timer-based idle detection) remain identical.
- `server-config`: Plist configuration loading is macOS-only. YAML remains the universal config path. No behavioral change — just platform availability of the plist option.
- `authentication`: HOPE and password hashing switch from CommonCrypto to platform-selected crypto backend. Behavioral requirements (SHA-1 HMAC, MD5 HMAC, password hashing) remain identical.

## Impact

- **Affected files** (platform-specific code to abstract):
  - `server.c` — kqueue calls (~200 lines) → platform event API
  - `tls.c` — SecureTransport (628 lines) → OpenSSL in `#else` block
  - `hope.c` — CommonCrypto SHA-1/MD5 (~250 lines) → platform crypto API
  - `password.c` — CommonCrypto SHA-1 (~120 lines) → platform crypto API
  - `encoding.c` — CoreFoundation (86 lines) → lookup table
  - `bonjour.c` — dns_sd (63 lines) → `#ifdef __APPLE__`
  - `config_plist.c` — CoreFoundation (165 lines) → `#ifdef __APPLE__`
  - `Makefile` — new Linux target, conditional framework flags
- **New files**: Platform headers in `include/hotline/platform/`, Linux backend implementations, Dockerfile
- **New build dependencies on Linux**: `libssl-dev`, `libcrypto-dev` (OpenSSL), `libyaml-dev`
- **No behavioral changes**: Every Hotline protocol feature works identically on both platforms. The abstraction is purely at the OS interface layer.
- **macOS remains fully supported**: This is additive — macOS builds continue to use native frameworks. No functionality removed.
- **Tiger (10.4 PPC) remains supported**: The platform abstraction does not affect the Tiger build path, which continues to use its existing compiler flags and framework versions.
