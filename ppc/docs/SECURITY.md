# Security: HOPE Encryption & TLS

## What does this protect?

By default, Hotline sends everything in plaintext — passwords, chat messages, file listings, and file transfers. Anyone on the network path can read all of it.

HOPE adds two layers of protection:

1. **Secure login** — your password is never sent over the wire. Instead, the server sends a random challenge and the client proves it knows the password by computing a MAC (message authentication code).
2. **Encrypted traffic** — after login, modern HOPE clients negotiate ChaCha20-Poly1305 AEAD, which encrypts chat, file listings, commands, and file transfers. If a client falls back to legacy RC4-only HOPE, chat, file listings, and commands stay encrypted, but file transfers do not.

**TLS** encrypts everything, including file transfers, but requires clients to connect on a separate port (default 5600).

For maximum security, leave HOPE on `Prefer AEAD` or `Require AEAD`, and optionally require TLS for E2E content if you also want the separate transfer connection wrapped in TLS.

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
| Enable HOPE Encryption | Activates challenge-response login and HOPE transport encryption |
| Legacy Mode | Allows older clients with weak algorithms (less secure) |
| E2E Prefix | Files/folders starting with this are hidden from non-encrypted clients |
| Cipher Policy | Chooses AEAD vs RC4 for HOPE clients; AEAD encrypts file transfers too |
| Require TLS for E2E | Clients must use HOPE and also connect over TLS to see E2E content |
| Require AEAD for E2E | Only AEAD-capable HOPE clients may access E2E content |
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

After authentication, Lemoniscate negotiates one of two HOPE transport modes:

- **AEAD (ChaCha20-Poly1305)** — encrypts chat, file listings, commands, and file transfers.
- **RC4** — encrypts chat, file listings, and commands, but file transfers still use a separate unencrypted connection unless TLS is also enabled.

The INVERSE algorithm is authentication-only — it does not establish transport encryption. This is why Legacy Mode is less secure: clients that negotiate INVERSE get authenticated but their subsequent traffic remains unencrypted.

### E2E Content Gating

The server checks `hl_client_is_encrypted()` to determine whether a client qualifies for E2E content access. This function verifies:

1. HOPE transport encryption is active on the connection.
2. The negotiated MAC algorithm is not INVERSE.
3. If `E2ERequireAEAD` is set, the HOPE connection is using AEAD rather than RC4.
4. If `E2ERequireTLS` is set, the HOPE connection is also on the TLS port.

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

| Connection | HOPE (AEAD) | HOPE (RC4) | TLS only | HOPE + TLS |
|-----------|--------------|------------|----------|------------|
| Login/password | Encrypted (MAC) | Encrypted (MAC) | Encrypted (TLS) | Both |
| Chat & commands | Encrypted (AEAD) | Encrypted (RC4) | Encrypted (TLS) | Both |
| File listings | Encrypted (AEAD) | Encrypted (RC4) | Encrypted (TLS) | Both |
| File transfers | Encrypted (AEAD) | **Plaintext** | Encrypted (TLS) | Encrypted |

Key takeaway: HOPE AEAD and TLS both protect file transfers. RC4-only HOPE does not.
