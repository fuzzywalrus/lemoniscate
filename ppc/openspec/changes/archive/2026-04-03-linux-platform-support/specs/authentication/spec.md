## ADDED Requirements

### Requirement: Cryptographic backend produces identical output on both platforms
The platform cryptographic abstraction SHALL produce byte-identical output for SHA-1 hashing, MD5 hashing, and HMAC computation on both macOS (CommonCrypto) and Linux (OpenSSL). This ensures that password hashes created on one platform can be verified on the other, and HOPE authentication works identically across platforms.

#### Scenario: Password hash portability
- **WHEN** a user account with a hashed password is created on macOS and the account YAML file is copied to a Linux server
- **THEN** the user can authenticate with the same password on the Linux server

#### Scenario: HOPE authentication cross-platform
- **WHEN** a HOPE-enabled client connects to a Linux server
- **THEN** the HMAC challenge-response produces the same result as it would on a macOS server with identical credentials
