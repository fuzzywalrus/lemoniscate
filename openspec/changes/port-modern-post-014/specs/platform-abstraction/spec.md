## ADDED Requirements

### Requirement: Platform-independent crypto interface
The server SHALL define a `platform_crypto.h` interface for cryptographic operations (hashing, random bytes) with platform-specific implementations. On PPC Tiger, the CommonCrypto backend SHALL be used.

#### Scenario: SHA-256 hash on PPC
- **WHEN** server code calls the platform crypto SHA-256 function
- **THEN** the CommonCrypto implementation SHALL compute the correct hash using `CC_SHA256`

#### Scenario: Random bytes on PPC
- **WHEN** server code requests cryptographic random bytes
- **THEN** the CommonCrypto implementation SHALL provide them via `SecRandomCopyBytes` or `/dev/urandom`

### Requirement: Platform-independent TLS interface
The server SHALL define a `platform_tls.h` interface for TLS operations. On PPC Tiger, the SecTransport backend SHALL be used exclusively.

#### Scenario: TLS handshake on PPC
- **WHEN** a client connects with TLS enabled
- **THEN** the SecTransport implementation SHALL perform the TLS handshake using the Tiger-compatible SecTransport API

#### Scenario: TLS certificate loading
- **WHEN** the server starts with TLS enabled and a certificate path configured
- **THEN** the SecTransport implementation SHALL load the certificate from the configured path

### Requirement: Platform-independent event loop interface
The server SHALL define a `platform_event.h` interface for I/O event multiplexing. On PPC Tiger, the kqueue backend SHALL be used.

#### Scenario: Register socket for read events
- **WHEN** server code registers a socket file descriptor for read readiness
- **THEN** the kqueue implementation SHALL add an `EVFILT_READ` kevent for that descriptor

#### Scenario: Event loop wait
- **WHEN** the event loop waits for events with a timeout
- **THEN** the kqueue implementation SHALL call `kevent()` and return ready descriptors

### Requirement: Platform-independent encoding interface
The server SHALL define a `platform_encoding.h` interface for text encoding conversion (Mac Roman ↔ UTF-8). On PPC Tiger, the CoreFoundation backend SHALL be used.

#### Scenario: Mac Roman to UTF-8 conversion
- **WHEN** server code converts a Mac Roman string to UTF-8
- **THEN** the CoreFoundation implementation SHALL use `CFStringCreateWithBytes` and produce correct UTF-8 output
