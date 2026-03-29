# Security: HOPE Encryption & TLS

## What does this protect?

By default, Hotline sends everything in plaintext — passwords, chat messages, file listings, and file transfers. Anyone on the network path can read all of it.

HOPE adds two layers of protection:

1. **Secure login** — your password is never sent over the wire. Instead, the server sends a random challenge and the client proves it knows the password by computing a MAC (message authentication code).
2. **Encrypted commands and chat** — after login, all transactions (chat, file listings, commands) are encrypted with RC4.

However, **HOPE does not encrypt file transfers**. File uploads and downloads happen on a separate connection and remain in plaintext even with HOPE enabled.

**TLS** encrypts everything, including file transfers, but requires clients to connect on a separate port (default 5600).

For maximum security, enable both HOPE and TLS, and turn on "Require TLS for E2E file transfers." This ensures passwords are challenge-response authenticated, all commands are RC4-encrypted, and all data (including file transfers) is wrapped in TLS.

## Quick setup

1. In the GUI, expand the **Security (HOPE)** section and check **Enable HOPE Encryption**.
2. Set an **E2E prefix** (e.g., `[E2E]`) — files and folders starting with this prefix will be hidden from non-encrypted clients.
3. Expand **TLS Encryption** and click **Generate Self-Signed...** to create a certificate.
4. Save settings (**Cmd+S**) and restart the server.
5. Verify the logs show `TLS enabled on 0.0.0.0:5600`.
6. Clients connect on port **5600** for TLS.

## What each option does

| Setting | What it does |
|---------|-------------|
| Enable HOPE Encryption | Activates challenge-response login and RC4 transport encryption |
| Legacy Mode | Allows older clients with weak algorithms (less secure) |
| E2E Prefix | Files/folders starting with this are hidden from non-encrypted clients |
| Require TLS for E2E | Clients must use TLS (port 5600) to see E2E content |
| TLS Certificate/Key | PEM files for TLS — use "Generate Self-Signed" or provide your own |
| TLS Port | The port for TLS connections (default 5600) |

---

## Technical details

### HOPE Protocol

HOPE stands for Hotline Online Protocol Extension.

During login, the server sends a random challenge to the client. The client computes `MAC(password, challenge)` and sends the result back. The server verifies by computing the same MAC with the stored password. No plaintext password ever crosses the wire.

Supported MAC algorithms:

- HMAC-SHA1
- HMAC-MD5
- SHA1
- MD5
- INVERSE (legacy mode only)

After authentication, both sides derive RC4 encryption keys from `MAC(password, session_key)`. This produces an encode key and a decode key. All subsequent transactions (chat, file listings, commands) are encrypted with RC4 using these keys.

The INVERSE algorithm is authentication-only — it does not establish transport encryption. This is why Legacy Mode is less secure: clients that negotiate INVERSE get authenticated but their subsequent traffic remains unencrypted.

### E2E Content Gating

The server checks `hl_client_is_encrypted()` to determine whether a client qualifies for E2E content access. This function verifies three conditions:

1. HOPE is active on the connection.
2. The negotiated MAC algorithm is not INVERSE (i.e., transport encryption is active).
3. If E2ERequireTLS is set, the client is connected over TLS.

Content gating behavior:

- **File listings** filter out entries whose names start with the E2E prefix for non-encrypted clients.
- **Directory access** to E2E-prefixed paths returns an error for non-encrypted clients.
- **File downloads and uploads** to E2E-prefixed paths are also gated.

### TLS Implementation

- Uses Apple's **SecureTransport** framework (not OpenSSL).
- PEM certificate and key are loaded via `SecItemImport` into a temporary keychain.
- A separate listener runs on the TLS port (default: main port + 100, i.e., 5600).
- File transfers also have a separate TLS port (TLS port + 1, i.e., 5601).
- Self-signed certificate generation uses `/usr/bin/openssl genrsa` and `openssl req -x509` with traditional RSA format for SecureTransport compatibility.

### What's encrypted where

| Connection | HOPE only | TLS only | HOPE + TLS |
|-----------|-----------|----------|------------|
| Login/password | Encrypted (MAC) | Encrypted (TLS) | Both |
| Chat & commands | Encrypted (RC4) | Encrypted (TLS) | Both |
| File listings | Encrypted (RC4) | Encrypted (TLS) | Both |
| File transfers | **Plaintext** | Encrypted (TLS) | Encrypted (TLS) |

This table is the key takeaway: HOPE alone leaves file transfers unprotected. For full encryption of all data, enable TLS as well.
