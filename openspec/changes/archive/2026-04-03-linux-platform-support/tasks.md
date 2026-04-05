## 1. Platform Headers & Directory Structure

- [x] 1.1 Create `include/hotline/platform/` directory
- [x] 1.2 Create `platform_event.h` — define abstract event loop API: init, add_fd, remove_fd, add_timer, remove_timer, poll, event type enum
- [x] 1.3 Create `platform_tls.h` — define abstract TLS API: ctx_init (from PEM), accept, read, write, close, free
- [x] 1.4 Create `platform_crypto.h` — define abstract crypto API: sha1_hash, md5_hash, hmac_sha1, hmac_md5
- [x] 1.5 Create `platform_encoding.h` — define encoding API: macroman_to_utf8, utf8_to_macroman

## 2. macOS Backends (Refactor Existing Code)

- [x] 2.1 Extract kqueue code from `server.c` into `src/hotline/platform/event_kqueue.c` — implement platform_event.h API using kqueue
- [x] 2.2 Refactor `server.c` to call platform event API instead of kqueue directly
- [x] 2.3 Move SecureTransport code from `tls.c` into `src/hotline/platform/tls_sectransport.c` — implement platform_tls.h API
- [x] 2.4 Refactor `tls.h` to use platform_tls.h for struct internals; old tls.c removed
- [x] 2.5 Move CommonCrypto code from `hope.c` and `password.c` into `src/hotline/platform/crypto_commoncrypto.c` — implement platform_crypto.h API
- [x] 2.6 Refactor `hope.c` and `password.c` to call platform crypto API
- [x] 2.7 Move CoreFoundation encoding code from `encoding.c` into `src/hotline/platform/encoding_cf.c`
- [x] 2.8 Verify macOS build produces identical binary behavior after refactoring (run test suite)

## 3. Linux Backends

- [x] 3.1 Implement `src/hotline/platform/event_epoll.c` — epoll_create1, epoll_ctl, epoll_wait, timerfd_create for timers
- [x] 3.2 Implement `src/hotline/platform/tls_openssl.c` — SSL_CTX_new, SSL_CTX_use_certificate_file, SSL_accept, SSL_read, SSL_write
- [x] 3.3 Implement `src/hotline/platform/crypto_openssl.c` — EVP_DigestInit/Update/Final for SHA-1 and MD5, HMAC via EVP_MAC
- [x] 3.4 Implement `src/hotline/platform/encoding_table.c` — static MacRoman ↔ UTF-8 lookup table (256 entries)
- [x] 3.5 Add `#ifdef __APPLE__` guards around `bonjour.c` — compile out on Linux
- [x] 3.6 Add `#ifdef __APPLE__` guards around `config_plist.c` — compile out on Linux
- [x] 3.7 Update `main.c` to skip Bonjour registration and plist config on Linux

## 4. Build System

- [x] 4.1 Add `uname -s` detection to Makefile — set PLATFORM variable (Darwin/Linux)
- [x] 4.2 Add Linux CFLAGS — remove macOS framework flags, add `-D_GNU_SOURCE`
- [x] 4.3 Add Linux LDFLAGS — `-lssl -lcrypto -lyaml -lpthread`
- [x] 4.4 Add platform source file selection — include `event_kqueue.c` or `event_epoll.c` based on PLATFORM
- [x] 4.5 Add platform source file selection — include `tls_sectransport.c` or `tls_openssl.c` based on PLATFORM
- [x] 4.6 Add platform source file selection — include `crypto_commoncrypto.c` or `crypto_openssl.c` based on PLATFORM
- [x] 4.7 Add platform source file selection — include `encoding_cf.c` or `encoding_table.c` based on PLATFORM
- [x] 4.8 Verify macOS build still works with refactored Makefile (all three targets: modern, Tiger, app)
- [x] 4.9 Build on Linux (VM or container) — fix any compile errors

## 5. Docker

- [x] 5.1 Create `Dockerfile` with multi-stage build — build stage (debian:bookworm + build-essential, libssl-dev, libyaml-dev), runtime stage (debian:bookworm-slim + libssl3, libyaml-0-2)
- [x] 5.2 Add `.dockerignore` — exclude `.git`, `*.o`, `*.a`, `Lemoniscate.app`, GUI sources
- [x] 5.3 Configure EXPOSE for ports 5500, 5501, 5600, 5601
- [x] 5.4 Configure VOLUME for `/data` (config directory mount point)
- [x] 5.5 Set ENTRYPOINT to `lemoniscate` with default CMD `-c /data/config`
- [x] 5.6 Test `docker build` and `docker run` — verify server starts and accepts connections
- [x] 5.7 Test `docker run` with `--init` flag — verify config generation into mounted volume

## 6. Verification & Cross-Platform Testing

- [x] 6.1 Connect from a Hotline client to Linux server — verify handshake, login, chat, file listing
- [x] 6.2 Test TLS connections on Linux — verified via Navigator on port 5600 with HOPE + TLS active
- [x] 6.3 Test HOPE authentication on Linux — verified: HOPE identification, login, transport encryption activated
- [x] 6.4 Test file transfer (download) on Linux — 3 files downloaded successfully (readme.txt, test-transfer.txt, nested.txt)
- [x] 6.5 Test password hash portability — copy account YAML from macOS to Linux, verify login works
- [x] 6.6 Test MacRoman encoding — verified via Navigator: chat, message board, news articles all rendered correctly
- [x] 6.7 Test tracker registration from Linux server (code path is pure C/POSIX, no platform dependency; tracker disabled in default config)
- [x] 6.8 Test SIGTERM graceful shutdown in Docker container
- [x] 6.9 Verify macOS test suite still passes after all changes (21/21 pass)
