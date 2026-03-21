# Lemoniscate Server Reference

## CLI Usage

```
lemoniscate [options]
```

### Options

| Flag | Long | Argument | Default | Description |
|------|------|----------|---------|-------------|
| `-i` | `--interface` | ADDR | all | IP address to listen on |
| `-p` | `--port` | PORT | 5500 | Base port (transfers on PORT+1) |
| | `--bind` | PORT | 5500 | Alias for `--port` (used by GUI launcher) |
| `-c` | `--config` | DIR | auto | Configuration directory path |
| `-f` | `--log-file` | PATH | stderr | Log file path (enables file logging) |
| `-l` | `--log-level` | LEVEL | info | Log level: `debug`, `info`, `error` |
| | `--init` | | | Initialize a default config directory and exit |
| `-v` | `--version` | | | Print version and exit |
| `-h` | `--help` | | | Show help |

### Examples

```bash
# Initialize a new config directory
./lemoniscate --init -c /path/to/config

# Start with defaults (port 5500, auto-find config)
./lemoniscate

# Start on a specific port with config directory
./lemoniscate -p 5500 -c /path/to/config

# Start with debug logging to a file
./lemoniscate -c /path/to/config -l debug -f /var/log/lemoniscate.log

# Bind to a specific interface
./lemoniscate -i 192.168.1.100 -p 5500 -c /path/to/config
```

### Signals

| Signal | Action |
|--------|--------|
| `SIGINT` / `SIGTERM` | Graceful shutdown |
| `SIGHUP` | Reload path exists, but live event-loop integration is currently incomplete |
| `SIGPIPE` | Ignored (prevents crash on broken client connections) |

---

## Configuration Directory Structure

Created by `--init` or manually:

```
config/
├── config.yaml          # Server configuration
├── Agreement.txt        # Login agreement shown to connecting clients
├── MessageBoard.txt     # Flat message board (news)
├── Banlist.yaml         # IP, username, and nickname bans
├── banner.jpg           # Server banner image (optional, JPG or GIF)
├── Files/               # Shared file directory (file root)
└── Users/               # User account YAML files
    ├── admin.yaml
    └── guest.yaml
```

### config.yaml

```yaml
Name: "My Hotline Server"
Description: "A Hotline server running Lemoniscate"
BannerFile: banner.jpg        # Relative to config dir, or absolute path
FileRoot: Files                # Relative to config dir, or absolute path
EnableTrackerRegistration: false
Trackers: []                   # e.g. ["tracker.example.com:5499"]
EnableBonjour: true
Encoding: macintosh            # Text encoding (macintosh = Mac Roman)
MaxDownloads: 0                # 0 = unlimited
MaxDownloadsPerClient: 0       # 0 = unlimited
MaxConnectionsPerIP: 0         # 0 = unlimited
PreserveResourceForks: false   # Preserve Mac resource forks in file transfers
```

### User Account Files (Users/*.yaml)

```yaml
Login: guest
Name: Guest User
Password: ""                   # Empty string = no password required
Access:
  DownloadFile: true
  UploadFile: false
  ReadChat: true
  SendChat: true
  CreateUser: false
  DeleteUser: false
  OpenUser: false
  ModifyUser: false
  GetClientInfo: true
  DisconnectUser: false
  Broadcast: false
  CreateFolder: false
  DeleteFile: false
  OpenChat: true
  NewsReadArt: true
  NewsPostArt: false
```

### Banlist.yaml

```yaml
banList: {}          # IP bans: {"192.168.1.100": true}
bannedUsers: {}      # Login bans: {"baduser": true}
bannedNicks: {}      # Nickname bans: {"troll": true}
```

### Agreement.txt

Plain text shown to users on first connection. If the file is missing or empty, no agreement is shown.

### MessageBoard.txt

The flat message board (news). Posts are prepended to the top. Uses `\r` (carriage return) as line separator per the Hotline protocol.

---

## Network

| Port | Purpose |
|------|---------|
| BASE (default 5500) | Hotline protocol (transactions, chat, user management) |
| BASE+1 (default 5501) | File transfers (HTXF protocol) |

### Connection Flow

1. TCP connection to base port
2. Hotline handshake (`TRTP` + version exchange)
3. Client sends `TranLogin` (107) with username, password, icon
4. Server checks ban list (IP + username)
5. Server authenticates against `Users/*.yaml` accounts
6. Server sends `TranShowAgreement` (109) if Agreement.txt exists
7. Client sends `TranAgreed` (121)
8. Server sends user access bitmap
9. Server broadcasts `TranNotifyChangeUser` (301) to all clients
10. Client is now connected and can send/receive transactions

### Rate Limiting

- Token bucket algorithm: 1 connection per 2 seconds per IP
- Excess connections are rejected with "Rate limited" log entry

### Idle Detection

- Checked every 10 seconds
- Users marked idle/away after 300 seconds (5 minutes) of inactivity
- KeepAlive and other transactions reset the idle timer

---

## Hotline Transactions (43 handlers)

### Chat & Messaging

| Type | Name | Description |
|------|------|-------------|
| 105 | ChatSend | Send public chat message |
| 106 | ChatMsg | Relayed chat (server → clients) |
| 108 | SendInstantMsg | Private message to a user |
| 112 | InviteNewChat | Create a new private chat room |
| 113 | InviteToChat | Invite user to existing chat room |
| 114 | RejectChatInvite | Decline chat invite |
| 115 | JoinChat | Join a private chat room |
| 116 | LeaveChat | Leave a private chat room |
| 120 | SetChatSubject | Set chat room topic |

### User Management

| Type | Name | Description |
|------|------|-------------|
| 107 | Login | Authenticate and connect |
| 121 | Agreed | Accept server agreement |
| 300 | GetUserNameList | Get list of online users |
| 301 | NotifyChangeUser | User status change broadcast |
| 302 | NotifyDeleteUser | User disconnect broadcast |
| 303 | GetClientInfoText | Get user info |
| 304 | SetClientUserInfo | Update own nick/icon |
| 348 | ListUsers | List user accounts (admin) |
| 349 | UpdateUser | Modify user account (admin) |
| 350 | NewUser | Create user account (admin) |
| 351 | DeleteUser | Delete user account (admin) |
| 352 | GetUser | Get user account details (admin) |
| 353 | SetUser | Set user account (admin) |
| 355 | UserBroadcast | Send broadcast message (admin) |
| 110 | DisconnectUser | Kick user (admin) |
| 500 | KeepAlive | Connection keepalive ping |

### Message Board (Flat News)

| Type | Name | Description |
|------|------|-------------|
| 101 | GetMsgs | Read the message board |
| 103 | OldPostNews | Post to the message board (prepends) |

### Threaded News

| Type | Name | Description |
|------|------|-------------|
| 370 | GetNewsCatNameList | Partial: handler currently returns empty reply with access checks |
| 371 | GetNewsArtNameList | Partial: handler currently returns empty reply with access checks |
| 400 | GetNewsArtData | Partial: handler currently returns empty reply with access checks |
| 410 | PostNewsArt | Partial: handler currently returns empty reply with access checks |
| 411 | DelNewsArt | Partial: handler currently returns empty reply with access checks |
| 380 | DelNewsItem | Partial: handler currently returns empty reply with access checks |
| 381 | NewNewsFldr | Partial: handler currently returns empty reply with access checks |
| 382 | NewNewsCat | Partial: handler currently returns empty reply with access checks |

### File Operations

| Type | Name | Description |
|------|------|-------------|
| 200 | GetFileNameList | List files in directory |
| 202 | DownloadFile | Partial: transfer setup only; transfer-port data path is currently stubbed |
| 203 | UploadFile | Partial: transfer setup only; transfer-port data path is currently stubbed |
| 204 | DeleteFile | Delete file/folder |
| 205 | NewFolder | Create folder |
| 206 | GetFileInfo | Get file metadata |
| 207 | SetFileInfo | Set file metadata |
| 208 | MoveFile | Move/rename file |
| 209 | MakeFileAlias | Create file alias |
| 210 | DownloadFolder | Partial: transfer setup only; transfer-port data path is currently stubbed |
| 213 | UploadFolder | Partial: transfer setup only; transfer-port data path is currently stubbed |
| 212 | DownloadBanner | Download server banner |

---

## REST API

**There is currently no REST API.** All server management is done through:

1. CLI flags at startup
2. Config file editing + restart (or future SIGHUP integration)
3. The Lemoniscate.app GUI (which manages the server process via NSTask)

A REST API (matching the Mobius Go server's `/api/v1/` endpoints) is planned for a future version.

See also: [GUI reference](GUI.md)

---

## Bonjour / Zeroconf

When `EnableBonjour: true` in config.yaml, the server registers as a `_hotline._tcp` service on the local network. Other Macs on the LAN can discover the server automatically in Hotline clients that support Bonjour browsing.

## Tracker Registration

When `EnableTrackerRegistration: true` and trackers are listed, the server periodically registers with Hotline tracker servers so it appears in public server lists.

---

## Building

### On Tiger (PowerPC)

```bash
# Install libyaml first (via Tigerbrew or from source)
brew install libyaml   # or build from source

# Build server only
make CC=gcc-4.0 \
  CFLAGS="-std=c99 -Wall -Wextra -pedantic -O2 -I./include -I/usr/local/include -DTARGET_OS_MAC=1 -mmacosx-version-min=10.4" \
  YAML_LDFLAGS="-L/usr/local/lib -lyaml" \
  lemoniscate

# Build GUI + server + app bundle
make CC=gcc-4.0 \
  CFLAGS="..." OBJCFLAGS="..." YAML_LDFLAGS="..." \
  app
```

### On modern macOS (development)

```bash
make all    # builds libhotline.a + lemoniscate
make test   # builds and runs all tests
```

---

## Version

Current: `0.1.2`

Based on [Mobius](https://github.com/jhalter/mobius) by Jeff Halter.
