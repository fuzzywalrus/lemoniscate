# Feature Parity Audit: Lemoniscate vs Mobius Go Reference

Audit date: 2026-03-20

Legend: [x] = implemented, [~] = partial/stub, [ ] = missing

---

## Transaction Handlers

### Chat & Messaging

- [x] **TranKeepAlive** (500) - Connection keepalive
- [x] **TranAgreed** (121) - Accept server agreement, set user info
- [x] **TranSetClientUserInfo** (304) - Change nickname/icon
- [x] **TranChatSend** (105) - Public and private chat messages, /me actions
- [x] **TranSendInstantMsg** (108) - Private messages, quoting, auto-reply, refuse PM check
- [x] **TranGetUserNameList** (300) - Connected user list
- [x] **TranGetClientInfoText** (303) - User info panel
- [x] **TranUserBroadcast** (355) - Admin broadcast to all users

### Private Chat Rooms

- [x] **TranInviteNewChat** (112) - Create private chat room + invite
- [x] **TranInviteToChat** (113) - Invite user to existing chat
- [x] **TranJoinChat** (115) - Join chat, get member list + subject
- [x] **TranLeaveChat** (116) - Leave chat, notify members
- [x] **TranRejectChatInvite** (114) - No-op (minimal server logic needed by design)
- [x] **TranSetChatSubject** (120) - Set chat room subject

### User Account Management

- [x] **TranGetUser** (352) - Get account details
- [x] **TranNewUser** (350) - Create account
- [x] **TranSetUser** (353) - Update account
- [x] **TranDeleteUser** (351) - Delete account
- [x] **TranListUsers** (348) - List all accounts
- [x] **TranUpdateUser** (349) - Batch account editor with nested subfield parsing (create/modify/rename/delete)
- [x] **TranDisconnectUser** (110) - Admin kick user

### Flat News / Message Board

- [x] **TranGetMsgs** (101) - Read message board
- [x] **TranOldPostNews** (103) - Post to message board

### Threaded News

- [x] **TranGetNewsCatNameList** (370) - List news categories
- [x] **TranGetNewsArtNameList** (371) - List articles in category
- [x] **TranGetNewsArtData** (400) - Read article content
- [x] **TranPostNewsArt** (410) - Post new article (with threading)
- [x] **TranDelNewsItem** (380) - Delete category/folder
- [x] **TranDelNewsArt** (411) - Delete article (with recursive option)
- [x] **TranNewNewsCat** (382) - Create news category
- [x] **TranNewNewsFldr** (381) - Create news folder

### File Operations

- [x] **TranGetFileNameList** (200) - List files in directory
- [x] **TranGetFileInfo** (206) - File metadata (type, creator, size, dates, comment)
- [x] **TranSetFileInfo** (207) - Update file comment/name
- [x] **TranDeleteFile** (204) - Delete file
- [x] **TranMoveFile** (208) - Move/rename file
- [x] **TranNewFolder** (205) - Create folder
- [x] **TranMakeFileAlias** (209) - Create symlink/alias

### File Transfers

- [x] **TranDownloadFile** (202) - File download with FILP wrapping
- [x] **TranUploadFile** (203) - File upload with FILP parsing, .incomplete staging, atomic rename
- [x] **TranDownloadFldr** (210) - Interactive multi-file protocol with recursive walk, FileHeaders, and per-file FILP
- [x] **TranUploadFldr** (213) - Interactive multi-file receive with path parsing, mkdir, and per-file FILP receive
- [x] **TranDownloadBanner** (212) - Banner image download
- [x] **File transfer resume** - Parses RFLT from FieldFileResumeData, seeks past offset in DATA fork. Advertises resume support via FIELD_FILE_TRANSFER_OPTS in download reply.

---

## Server Features

### Networking & Infrastructure

- [x] Rate limiting (per-IP token bucket)
- [x] Idle/away detection (300s timeout, 10s check interval)
- [x] Ban list (IP + username banning)
- [x] kqueue event loop (Tiger 10.4+ native)
- [x] Dual-port system (5500 main + 5501 transfers)
- [x] TLS/SSL support (SecureTransport, TLS 1.0 — see implementation notes below)
- [ ] Redis integration (Mobius uses for bans, online tracking)

### Discovery & Registration

- [x] Tracker registration (UDP, periodic every 300s with live user count, config-driven)
  > **Note:** Packets send correctly but tested trackers (preterhuman, mainecyber, badmoon) don't list us. Trackers do a TCP connect-back probe with zero data — our 10s handshake timeout may be too slow. Needs further investigation with other trackers or tracker software.
- [x] Bonjour/mDNS (dns_sd.h integration, registers on local network)

> Note: Mobius has tracker registration but NO Bonjour. We have both, though tracker listing isn't working yet with public trackers.

### Login & Session

- [x] Handshake protocol
- [x] Login authentication (account lookup + password verify)
- [x] Server agreement display (with skip permission)
- [x] Server banner support (via transfer port)
- [x] User join/leave notifications (TranNotifyChangeUser / TranNotifyDeleteUser)
- [x] Admin flag detection (ACCESS_DISCON_USER)
- [x] Password hashing (salted SHA-1 via OpenSSL 0.9.7 on Tiger; bcrypt unavailable without bundling. Format: `sha1:<salt>:<hash>`. Falls back to plaintext comparison for migrating old accounts)

### User Notifications

- [x] User join broadcast (individual fields: ID, icon, flags, name)
- [x] User leave broadcast
- [x] User info change broadcast (name/icon change)
- [x] Away status broadcast (idle timeout + activity resume)

---

## Access Control (41 permission bits)

### Actively Checked (39 of 41)

- [x] ACCESS_DELETE_FILE (0)
- [x] ACCESS_UPLOAD_FILE (1)
- [x] ACCESS_DOWNLOAD_FILE (2)
- [x] ACCESS_RENAME_FILE (3)
- [x] ACCESS_MOVE_FILE (4)
- [x] ACCESS_CREATE_FOLDER (5)
- [x] ACCESS_READ_CHAT (9)
- [x] ACCESS_SEND_CHAT (10)
- [x] ACCESS_OPEN_CHAT (11)
- [x] ACCESS_CREATE_USER (14)
- [x] ACCESS_DELETE_USER (15)
- [x] ACCESS_OPEN_USER (16)
- [x] ACCESS_MODIFY_USER (17)
- [x] ACCESS_NEWS_READ_ART (20)
- [x] ACCESS_NEWS_POST_ART (21)
- [x] ACCESS_DISCON_USER (22)
- [x] ACCESS_CANNOT_BE_DISCON (23)
- [x] ACCESS_GET_CLIENT_INFO (24)
- [x] ACCESS_ANY_NAME (26)
- [x] ACCESS_NO_AGREEMENT (27)
- [x] ACCESS_SET_FILE_COMMENT (28)
- [x] ACCESS_MAKE_ALIAS (31)
- [x] ACCESS_BROADCAST (32)
- [x] ACCESS_NEWS_DELETE_ART (33)
- [x] ACCESS_NEWS_CREATE_CAT (34)
- [x] ACCESS_NEWS_DELETE_CAT (35)
- [x] ACCESS_NEWS_CREATE_FLDR (36)
- [x] ACCESS_NEWS_DELETE_FLDR (37)
- [x] ACCESS_UPLOAD_FOLDER (38)
- [x] ACCESS_DOWNLOAD_FOLDER (39)
- [x] ACCESS_SEND_PRIV_MSG (40)

### Not Implemented (1 of 41) — unused in Mobius too

- [ ] ACCESS_CLOSE_CHAT (12) - protocol artifact, not implemented in any known server

### Recently Added

- [x] ACCESS_DELETE_FOLDER (6) - delete handler checks file vs folder
- [x] ACCESS_RENAME_FOLDER (7) - set_file_info checks file vs folder for rename
- [x] ACCESS_SHOW_IN_LIST (13) - users without this bit are hidden from user list
- [x] ACCESS_UPLOAD_ANYWHERE (25) - uploads restricted to "Uploads" or "Drop Box" folders
- [x] ACCESS_SET_FOLDER_COMMENT (29) - set_file_info checks folder comment permission
- [x] ACCESS_VIEW_DROP_BOXES (30) - file listing blocks drop box access without permission

---

## Priority TODO

### High Priority (core functionality gaps)

1. ~~**File upload receive** - TranUploadFile is a stub; need to accept incoming file data on transfer port~~ DONE
2. ~~**File transfer resume** - Clients expect RFLT resume support for interrupted transfers~~ DONE
3. ~~**Tracker registration** - Required for public server visibility~~ DONE (code works, trackers not listing — investigate later)
4. ~~**Password hashing** - Plaintext passwords are a security risk; integrate bcrypt~~ DONE (salted SHA-1 via Tiger's OpenSSL)

### Medium Priority (completeness)

5. ~~**Folder download** - TranDownloadFldr needs folder archiving + multi-file transfer~~ DONE
6. ~~**Folder upload** - TranUploadFldr needs folder receive logic~~ DONE
7. ~~**TranUpdateUser** (349) - Batch account editor for v1.5+ clients~~ DONE
8. ~~**ACCESS_CANNOT_BE_DISCON** check~~ Already implemented
9. ~~**Missing folder permission checks**~~ Most already implemented (RENAME_FILE, CREATE_FOLDER, NEWS_CREATE/DELETE_CAT/FLDR). Remaining: DELETE_FOLDER, RENAME_FOLDER (need file-vs-folder distinction)

### Low Priority (polish)

10. ~~**ACCESS_SHOW_IN_LIST** - Hide users from user list~~ DONE
11. ~~**ACCESS_VIEW_DROP_BOXES** - Drop box folder visibility~~ Already implemented
12. ~~**ACCESS_CHANGE_OWN_PASS** - Self-service password change~~ DONE (piggybacks on SetUser 353)
13. ~~**TLS/SSL support** - Encrypted connections~~ DONE (SecureTransport, TLS 1.0)
14. **Encoding support** - Mobius has UTF-8/MacRoman config; we assume MacRoman
15. ~~**malloc error on banner download**~~ FIXED — transfer manager uses internal array, not heap; callers were incorrectly free()ing the pointer

---

## TLS Implementation Notes

### Overview

TLS support uses Apple's **SecureTransport** framework (not OpenSSL) to provide encrypted
connections on separate TLS ports. This mirrors the Go reference implementation which uses
`crypto/tls`.

### Tiger 10.4 Compatibility

Tiger's SecureTransport supports up to **TLS 1.0**. The implementation uses Tiger-era APIs:

| Modern macOS API (10.7+)              | Tiger API used instead               |
|---------------------------------------|--------------------------------------|
| `SSLCreateContext`                    | `SSLNewContext` (10.2+)              |
| `CFRelease(ssl)` for SSL contexts    | `SSLDisposeContext` (10.2+)          |
| `SecItemImport`                       | `SecKeychainItemImport` (10.0+)      |
| `SecIdentityCreateWithCertificate`    | `SecIdentitySearchCreate` (10.0+)    |

All I/O callbacks (`SSLSetIOFuncs`, `SSLRead`, `SSLWrite`, `SSLHandshake`, `SSLClose`) are
unchanged — they've been available since 10.2.

### Architecture

- **Separate TLS ports**: TLS listens on a configurable port (default: base port + 100),
  e.g., 5600/5601 when the main server runs on 5500/5501.
- **Connection wrapper** (`hl_tls_conn_t`): Unified I/O abstraction that routes reads/writes
  through either plain BSD sockets or SecureTransport SSL. Plain connections also use the
  wrapper for consistent code paths.
- **Layering**: TLS sits below the application protocol. HOPE encryption (if also active)
  layers on top — though TLS makes HOPE transport encryption redundant, they can coexist.
- **PEM loading**: Certificates and private keys are loaded from PEM files into a temporary
  Keychain, then a `SecIdentityRef` is built from the cert+key pair.

### Configuration

Config file (`config.yaml`):
```yaml
TLSCertFile: ./tls/cert.pem
TLSKeyFile: ./tls/key.pem
TLSPort: 5600
```

CLI flags (override config):
```
--tls-cert PATH    PEM certificate file
--tls-key PATH     PEM private key file
--tls-port PORT    TLS base port
```

### Client Compatibility

Tiger's SecureTransport supports up to **TLS 1.0 only**. This creates a compatibility
constraint with modern clients:

- **Hotline Navigator** uses rustls by default, which requires TLS 1.2+. TLS connections
  from Navigator to a Tiger server will fail with `errSSLProtocol (-9801)` / "tls handshake
  eof" unless Navigator's `allowLegacyTls` option is enabled, which switches to `native-tls`
  (the OS's TLS stack, which supports TLS 1.0 on macOS).
- **Classic Hotline 1.x clients** do not support TLS at all. They connect on the standard
  plaintext port.
- **curl, wget, or other tools** with `--tlsv1.0` will work for testing.
- **HOPE encryption** is the recommended encryption method for Navigator ↔ Lemoniscate
  connections. It works at the application layer with no TLS version constraints, and
  provides challenge-response auth plus negotiated AEAD or RC4 transport encryption.

The server always accepts plaintext + HOPE connections on the standard port (5500), so
TLS is purely additive. The three encryption options from weakest to strongest:

1. **Plaintext** — no encryption (legacy default)
2. **HOPE** — application-layer challenge-response auth + negotiated AEAD or RC4 transport encryption
3. **TLS 1.0** — transport-layer encryption (requires TLS 1.0-capable client)

All three can coexist on the same server. HOPE can even layer on top of TLS (redundant
but harmless).

---

## HOPE Secure Login Implementation Notes

### What is HOPE?

HOPE (Hotline Open Protocol Extensions) is a community-developed extension to the Hotline
protocol that adds challenge-response authentication and optional transport encryption.
It replaces the legacy login flow (where credentials are trivially obfuscated with 255-XOR)
with MAC-based authentication using a session key, and can optionally wrap the connection
in either RC4 or ChaCha20-Poly1305 AEAD transport encryption.

The spec was developed by fogWraith and is documented in `HOPE-Secure-Login.md` in the
Hotline community archives. Lemoniscate implements HOPE as an opt-in feature controlled
by the `EnableHOPE` config flag.

### Protocol Flow

HOPE adds a three-phase negotiation before the normal login:

```
Client                              Server
  |                                    |
  |  Phase 1: Login(login=0x00)        |   HOPE probe — single null byte
  |  + HopeMacAlgorithm (list)        |   signals "I want HOPE"
  |  + HopeServerCipher (list)        |
  |----------------------------------->|
  |                                    |
  |  Phase 2: Reply                    |   Server picks strongest MAC,
  |  + HopeSessionKey (64 bytes)       |   generates 64-byte session key,
  |  + HopeMacAlgorithm (selection)    |   echoes cipher choice
  |  + UserLogin (non-empty = MAC it)  |
  |  + HopeServerCipher (selection)    |
  |<-----------------------------------|
  |                                    |
  |  Phase 3: Login(MAC'd creds)       |   Client MACs login+password
  |  + UserLogin = MAC(login, key)     |   with the session key
  |  + UserPassword = MAC(pw, key)     |
  |  + UserName, UserIconId, etc.      |
  |----------------------------------->|
  |                                    |
  |  Normal login reply                |   If cipher was negotiated,
  |<-----------------------------------|   transport encryption starts here
```

### Detection

A HOPE probe is detected in `hl_hope_detect()` by checking if the `FIELD_USER_LOGIN` in
the login transaction is exactly one byte with value `0x00`. Non-HOPE clients send
multi-byte 255-XOR encoded login names, so there's no ambiguity.

Detection only fires when `EnableHOPE` is true in config. If disabled, the probe falls
through to the normal login path where the 0x00 byte is decoded as an invalid username.

### MAC Algorithm Negotiation

The client sends a list of supported MAC algorithms. The server picks the strongest one
it supports. The MAC algorithm IDs and their strength ordering:

| ID | Name        | Type                    | Strength |
|----|-------------|-------------------------|----------|
| 0  | INVERSE     | 255-XOR (legacy compat) | Weakest  |
| 1  | MD5         | MD5(key + message)      |          |
| 2  | HMAC-MD5    | HMAC-MD5(key, message)  |          |
| 3  | SHA1        | SHA1(key + message)     |          |
| 4  | HMAC-SHA1   | HMAC-SHA1(key, message) | Strongest|

INVERSE is always supported as a fallback per spec.

### Wire Format for Algorithm/Cipher Fields

Algorithm and cipher fields use a length-prefixed list format on the wire:

```
<u16:count> [<u8:name_len> <name_bytes>]+
```

For the server's selection reply, count is always 1. This format is critical for
interop with Navigator, which calls `decode_algorithm_selection()` expecting the
count prefix.

### Session Key

The 64-byte session key is generated by the server:
- Bytes 0-3: Server IP (big-endian)
- Bytes 4-5: Client port (big-endian)
- Bytes 6-63: Random (from `RAND_bytes`, fallback to LCG if unavailable)

### MAC'd Login Resolution

When the server tells the client to MAC the login (by sending a non-empty
`FIELD_USER_LOGIN` in the Phase 2 reply), the client computes
`MAC(login_bytes, session_key)` and sends the result. The server cannot reverse
this MAC, so it must iterate all known accounts, compute `MAC(account_login, session_key)`
for each, and find the match.

This is implemented in the HOPE login decode path in `server.c`. For INVERSE mode,
the login is simply 255-XOR decoded (same as legacy).

### Password Verification

The client sends `MAC(password_bytes, session_key)` as the password field. The server
computes the same MAC using the stored password and compares:

- **HOPE-encrypted passwords** (`hope:` prefix in account file): The server decrypts
  the password using the HOPE master key, then computes the MAC for comparison.
- **INVERSE fallback**: The password is 255-XOR decoded and verified against the
  stored SHA-1 hash using `hl_password_verify()`.
- **Empty passwords**: For accounts with no password (like guest), the server verifies
  `MAC("", session_key)` matches the client's MAC.

### Transport Encryption (Optional)

If both sides negotiate a cipher (currently RC4), transport encryption is activated
after the Phase 3 login is verified:

1. `encode_key = MAC(password_bytes, password_mac)`
2. `decode_key = MAC(password_bytes, encode_key)`
3. RC4 cipher state is initialized with these keys
4. All subsequent reads/writes are encrypted/decrypted through `hl_hope_read()`
   and `hl_hope_write()`

Transport encryption layers on top of TLS if both are active (though this is
redundant — TLS alone provides equivalent or better security).

### HOPE Master Key

When HOPE is enabled, the server loads or generates a 32-byte master key from
`<config_dir>/hope.key`. This key is used to encrypt passwords at rest in
`hope:<hex>` format, which allows the server to recover plaintext passwords
for HMAC verification. The key file is created with 0600 permissions on first run.

### Configuration

```yaml
EnableHOPE: true        # Enable HOPE secure login
```

CLI: no separate flag — controlled by config file only.

GUI: "Security (HOPE)" panel in the settings sidebar with Enable checkbox,
Legacy Mode checkbox, and E2E Prefix field.

### Source Files

| File | Purpose |
|------|---------|
| `include/hotline/hope.h` | API declarations, MAC/cipher constants |
| `src/hotline/hope.c` | MAC computation, key derivation, RC4 I/O, password encryption |
| `include/hotline/types.h` | HOPE field type constants (0x0E01-0x0ECA) |
| `src/hotline/server.c` | HOPE detection, negotiation, Phase 3 login handling |
| `src/mobius/config_plist.c` | EnableHOPE plist loading |
| `src/mobius/config_loader.c` | EnableHOPE YAML loading |

### Client Compatibility

Tested with **Hotline Navigator** (Tauri/Rust). The client must have HOPE explicitly
enabled per-bookmark (`bookmark.hope = true`). If the HOPE probe fails, Navigator
reconnects and falls back to legacy login automatically.

Classic Hotline 1.x clients do not support HOPE and will always use the legacy
login path. The server handles both transparently.
