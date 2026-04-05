## ADDED Requirements

### Requirement: Plaintext login with 255-rotation obfuscation
The server SHALL accept login transactions containing a username and password field, where the password is obfuscated using 255-rotation encoding (each byte XOR'd with 0xFF). The server decodes the password and validates it against the stored account.

#### Scenario: Valid login
- **WHEN** a client sends a login transaction with a valid username and correctly obfuscated password
- **THEN** the server authenticates the client, assigns a client ID, and sends a login reply with the server name, version, and banner (if configured)

#### Scenario: Invalid password
- **WHEN** a client sends a login transaction with a valid username but incorrect password
- **THEN** the server replies with an error transaction and does not grant access

#### Scenario: Unknown username
- **WHEN** a client sends a login transaction with a username that has no matching account
- **THEN** the server replies with an error transaction

### Requirement: Guest access without credentials
The server SHALL allow guest login when a "guest" account exists. A client MAY omit the username and password fields to log in as guest.

#### Scenario: Guest login with guest account enabled
- **WHEN** a client sends a login transaction with no username/password and a guest account exists
- **THEN** the server authenticates the client with guest-level permissions

#### Scenario: Guest login with no guest account
- **WHEN** a client sends a login transaction with no credentials and no guest account is configured
- **THEN** the server replies with an error transaction

### Requirement: HOPE secure login with SHA-1 challenge-response
The server SHALL support HOPE (Hotline One-time Password Extension) secure login. After handshake, the server sends a HOPE challenge containing a session key and MAC algorithm identifier. The client responds with an HMAC-SHA1 (or other negotiated MAC) of the password using the session key. The server verifies the MAC against the stored password hash.

#### Scenario: Successful HOPE authentication
- **WHEN** a client receives the HOPE challenge, computes the correct MAC of its password with the session key, and sends the login transaction with the MAC
- **THEN** the server verifies the MAC matches and grants access

#### Scenario: HOPE MAC mismatch
- **WHEN** a client sends a HOPE login with an incorrect MAC value
- **THEN** the server rejects the login with an error

#### Scenario: HOPE algorithm negotiation
- **WHEN** the server sends a HOPE challenge with a MAC algorithm field
- **THEN** the client uses the specified algorithm (HMAC-SHA1, SHA1, HMAC-MD5, MD5, or inverse) for MAC computation

### Requirement: TLS encryption for transport security
The server SHALL support TLS encryption for the protocol connection when configured. TLS is negotiated after the TCP handshake but before the Hotline handshake, wrapping all subsequent protocol traffic in an encrypted tunnel.

#### Scenario: TLS-enabled connection
- **WHEN** a client connects to a TLS-enabled server and completes the TLS handshake
- **THEN** all subsequent Hotline protocol traffic (handshake, login, transactions) is encrypted

#### Scenario: TLS not configured
- **WHEN** the server is running without TLS configuration
- **THEN** connections proceed with plaintext Hotline protocol

### Requirement: Login reply includes server metadata
Upon successful authentication, the server SHALL reply with a login response containing the server name, server version (190), and optionally the banner image data and community banner URL.

#### Scenario: Login reply with banner
- **WHEN** a client successfully authenticates and the server has a banner configured
- **THEN** the login reply includes the server name, version 190, and the banner image data

#### Scenario: Login reply without banner
- **WHEN** a client successfully authenticates and no banner is configured
- **THEN** the login reply includes the server name and version but no banner field

### Requirement: Agreement display after login
After successful login, the server SHALL send the agreement text (if configured) to the client. The client must acknowledge the agreement before proceeding with other transactions.

#### Scenario: Agreement sent after login
- **WHEN** a client logs in and the server has an agreement file configured
- **THEN** the server sends the agreement text to the client immediately after the login reply

#### Scenario: No agreement configured
- **WHEN** a client logs in and no agreement file exists
- **THEN** the server skips the agreement step and the client can proceed immediately
