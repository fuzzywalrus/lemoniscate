## ADDED Requirements

### Requirement: Compile-time platform selection
The build system SHALL select platform-specific implementations at compile time based on the host operating system. macOS builds SHALL use native frameworks (kqueue, SecureTransport, CommonCrypto, CoreFoundation). Linux builds SHALL use POSIX/Linux APIs (epoll, OpenSSL, libcrypto) and static lookup tables.

#### Scenario: Build on macOS
- **WHEN** the project is built on macOS (Darwin)
- **THEN** the Makefile selects kqueue, SecureTransport, CommonCrypto, CoreFoundation, and Bonjour implementations

#### Scenario: Build on Linux
- **WHEN** the project is built on Linux
- **THEN** the Makefile selects epoll, OpenSSL, libcrypto implementations and excludes Bonjour and plist support

### Requirement: Platform event loop abstraction
The server SHALL use an abstracted event loop API for I/O multiplexing and timer management. The API SHALL support registering file descriptors for read events, registering periodic timers, polling for events with a timeout, and iterating results. The macOS backend SHALL use kqueue. The Linux backend SHALL use epoll with timerfd.

#### Scenario: Register fd for read events on macOS
- **WHEN** the server registers a socket for read events on macOS
- **THEN** a kqueue EVFILT_READ event is added for that fd

#### Scenario: Register fd for read events on Linux
- **WHEN** the server registers a socket for read events on Linux
- **THEN** an epoll EPOLLIN event is added for that fd

#### Scenario: Register periodic timer on Linux
- **WHEN** the server registers a periodic timer on Linux
- **THEN** a timerfd is created and registered with epoll for EPOLLIN events

#### Scenario: Event polling returns same results on both platforms
- **WHEN** a socket has data available and the event loop polls
- **THEN** the abstracted event API returns the same event type and associated data on both macOS and Linux

### Requirement: Platform TLS abstraction
The server SHALL use an abstracted TLS API for encrypted connections. The API SHALL support context initialization from PEM cert/key files, accepting TLS connections on sockets, reading and writing encrypted data, and closing connections. The macOS backend SHALL use SecureTransport. The Linux backend SHALL use OpenSSL.

#### Scenario: TLS accept on macOS
- **WHEN** a client connects to the TLS port on macOS
- **THEN** a SecureTransport SSL context performs the handshake

#### Scenario: TLS accept on Linux
- **WHEN** a client connects to the TLS port on Linux
- **THEN** an OpenSSL SSL context performs the handshake

#### Scenario: TLS read/write behavior is identical
- **WHEN** the server reads or writes data over a TLS connection
- **THEN** the abstracted API behaves identically on both platforms (same data, same error semantics)

#### Scenario: PEM cert/key loading on Linux
- **WHEN** the server loads TLS certificates from PEM files on Linux
- **THEN** OpenSSL loads the cert and key directly without keychain workarounds

### Requirement: Platform cryptographic abstraction
The server SHALL use an abstracted crypto API for SHA-1 hashing, MD5 hashing, and HMAC computation. The macOS backend SHALL use CommonCrypto. The Linux backend SHALL use OpenSSL EVP APIs.

#### Scenario: SHA-1 password hash on Linux
- **WHEN** a user authenticates with a password on Linux
- **THEN** the password is hashed using OpenSSL EVP SHA-1, producing identical output to CommonCrypto SHA-1

#### Scenario: HOPE HMAC on Linux
- **WHEN** a HOPE authentication challenge is processed on Linux
- **THEN** the HMAC-SHA1 and HMAC-MD5 computations produce identical results to CommonCrypto

### Requirement: MacRoman encoding without CoreFoundation
The server SHALL convert between MacRoman and UTF-8 text encoding using a platform-independent implementation. On Linux (and optionally macOS), a static 256-byte lookup table SHALL be used instead of CoreFoundation string APIs.

#### Scenario: MacRoman to UTF-8 on Linux
- **WHEN** the server converts a MacRoman-encoded Hotline string to UTF-8 on Linux
- **THEN** the output is byte-identical to the CoreFoundation conversion on macOS

#### Scenario: UTF-8 to MacRoman on Linux
- **WHEN** the server converts a UTF-8 string to MacRoman for a classic Hotline client on Linux
- **THEN** the output is byte-identical to the CoreFoundation conversion on macOS

### Requirement: macOS-only features gracefully absent on Linux
Features that are macOS-only (Bonjour service registration, plist configuration loading) SHALL be compiled out on Linux. The server SHALL start and operate normally without these features. No stub implementations or runtime errors SHALL occur.

#### Scenario: Bonjour disabled on Linux
- **WHEN** the server starts on Linux with `EnableBonjour: true` in config
- **THEN** the Bonjour feature is silently unavailable and the server logs that Bonjour is not supported on this platform

#### Scenario: No plist config on Linux
- **WHEN** the server starts on Linux
- **THEN** only YAML configuration loading is available and plist loading is not attempted
