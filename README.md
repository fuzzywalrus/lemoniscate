# Lemoniscate - A native Hotline server in C and Objective-C


A native implementation of the Hotline protocol with a full-featured AppKit admin GUI for macOS.

![Lemoniscate](docs/images/lemoniscate.png)


## What is this?

Lemoniscate is a Hotline server written from the ground up in C and Objective-C. It was originally inspired by [Mobius](https://github.com/jhalter/mobius), a modern Hotline server written in Go by Jeff Halter, but has significantly diverged into its own project with features that have no upstream counterpart: HOPE challenge-response encryption, TLS support, end-to-end file content gating, a native AppKit admin GUI with disclosure sections and help popovers, and self-signed certificate generation.

The `modern` branch targets macOS 10.11 El Capitan and later. The `main` branch targets Mac OS X 10.4 Tiger on PowerPC.

See the releases for the latest version!

## What is Hotline?

Hotline was a client-server platform from the late 1990s that combined file sharing, group chat, instant messaging, and a news bulletin board into a single application. It was especially popular on the Mac and had a devoted community through the early 2000s. Hotline servers were run by individuals and small communities, each with their own files, chat rooms, and culture. Think of it as a self-hosted Discord from 1997, minus the voice chat and plus a built-in file server.

The Hotline protocol is a binary, big-endian, TCP-based protocol. This is actually a nice fit for PowerPC, which is also big-endian -- the wire format maps directly to native memory layout with no byte-swapping required.

## Project structure

The codebase is split into two layers:

- `include/hotline/` and `src/hotline/` -- Core protocol library: wire format parsing, serialization, handshake, user/access types, text encoding, and the Objective-C client. Includes HOPE encryption (`hope.c`, `hope.h`) and TLS support (`tls.c`, `tls.h`). Builds into `libhotline.a`.
- `include/mobius/` and `src/mobius/` -- Server application: transaction handlers, YAML-based persistence (accounts, bans, threaded news), configuration loading, and runtime server behavior. The `mobius/` directory name is historical.
- `src/gui/` -- Native AppKit admin GUI (`lemoniscate-gui`) that launches and supervises `lemoniscate`.
- `docs/` -- Operator and implementation documentation.

## Quickstart to rolling your own (CLI server)

### 1) Build

On modern macOS:

```bash
make all
```

### 2) Initialize config directory

```bash
./lemoniscate --init -c ./config
```

This creates:
- `config/config.yaml`
- `config/Users/*.yaml` (including `admin` and `guest`)
- `config/Agreement.txt`
- `config/MessageBoard.txt`
- `config/Banlist.yaml`
- `config/Files/`

### 3) Start server

```bash
./lemoniscate -p 5500 -c ./config
```

Clients connect on port `5500` (file transfers use `5501`).

## Quickstart (GUI)

### 1) Build GUI and app bundle

```bash
make gui
make app
```

### 2) Launch app bundle

```bash
open Lemoniscate.app
```

The app launches `Lemoniscate.app/Contents/MacOS/lemoniscate-server` through `NSTask`.

Important caveats:
- `make app` checks that `lemoniscate` and `lemoniscate-gui` have matching CPU architectures.
- Use `APP_SKIP_ARCH_CHECK=1 make app` only when intentionally bypassing that safety check.
- The GUI creates the config directory if missing, but does not scaffold full default files; run `--init` first for a complete setup.

## Feature status

### Chat & Messaging

- Public chat room with all connected users
- Private chat rooms — create, invite users, set topics
- Instant messages (private messages between users)
- Auto-reply when a user is away or idle
- /me action messages
- Admin broadcast messages to all users
- Persistent public chat history (opt-in) — capability-negotiated, cursor-paginated, with optional ChaCha20-Poly1305 body encryption at rest. Configure under `ChatHistory:` in `config.yaml`; see [config/config.yaml.example](config/config.yaml.example) and [docs/Capabilities-Chat-History.md](docs/Capabilities-Chat-History.md).

### User Management

- User accounts with login and password (salted SHA-1 hashed)
- Guest access (no login required)
- 41 individual permission bits per account
- Admin account editor — create, modify, rename, and delete accounts
- Batch account editing (v1.5+ multi-user editor)
- Kick/disconnect users (with optional message)
- Protected accounts that can't be disconnected

### File Sharing

- Browse server files and folders
- Download files with progress tracking
- Upload files to the server
- Download entire folders (recursive)
- Upload entire folders (recursive)
- Resume interrupted downloads
- File info — type, creator, size, dates, comments
- Rename, move, and delete files and folders
- Create new folders
- Create file aliases (symlinks)
- 70+ file type mappings for correct Mac type/creator codes

### News & Message Board

- Flat message board (classic Hotline "News")
- Threaded news with categories and articles
- Create and delete news categories
- Post, read, and delete articles with threading
- News data persists across server restarts

### Security

- HOPE challenge-response authentication (replaces plaintext password login)
- HOPE RC4 transport encryption for the transaction channel
- E2E file content gating (prefix-based, hidden from non-encrypted clients)
- Optional TLS requirement for E2E file transfers
- TLS/SSL with self-signed certificate generation from the GUI
- Configurable E2E content prefix

### Server Administration

- GUI admin application (Cocoa, native AppKit)
- Setup wizard for first-time configuration
- Collapsible disclosure sections with help popovers
- Self-signed TLS certificate generation
- macOS plist configuration (native format)
- YAML fallback for compatibility
- Server agreement displayed on login
- Server banner image
- Default banner bundled with the app
- Start, stop, and restart from the GUI
- Live server logs in the admin interface
- Log file saved to Application Support

### Networking

- Hotline protocol on port 5500 (configurable)
- File transfers on port 5501
- TLS/SSL encryption on separate port (default 5600)
- HOPE secure authentication protocol
- Bonjour/mDNS for local network discovery
- Tracker registration (UDP, periodic with live user count)
- Idle/away detection (5 minutes, auto-broadcasts status)
- Per-IP rate limiting
- Ban list (IP addresses and usernames)

## Access Control

Granular per-account permissions including:

- Download, upload, delete, rename, and move files
- Create folders and file aliases
- Read and send chat
- Create and manage private chat rooms
- Read, post, and delete news articles
- Create and delete news categories
- View and modify user accounts
- Disconnect other users
- Send broadcast messages
- Upload location restrictions (Uploads/Drop Box only)
- Drop box folder visibility control

### Compatibility

- Runs on macOS 10.11+ (modern branch), Mac OS X 10.4 Tiger (main branch), and Linux (x86_64)
- Docker support for Linux, macOS, and Windows deployment
- Works with Hotline Navigator, the mierau Swift client, and classic Hotline clients
- FILP file transfer format with INFO and DATA forks
- Hotline 1.8+ protocol (version 190)
- CLI server binary for headless operation
- Compatible account and news file formats (YAML-based)


## Deployment

Lemoniscate runs on macOS (native or Docker), Linux (native or Docker), and Windows (Docker).

| Platform | Native | Docker |
|----------|--------|--------|
| macOS | Build with `make all` | [Docker on macOS](docs/DOCKER-MACOS.md) |
| Linux | [Build from source](docs/LINUX.md) | [Docker on Linux](docs/DOCKER-LINUX.md) |
| Windows | -- | [Docker on Windows](docs/DOCKER-WINDOWS.md) |

A complete configuration reference with all options is in [`config/config.yaml.example`](config/config.yaml.example).

## Building (macOS)

On modern macOS for development and testing:

```bash
make all       # build libhotline.a + lemoniscate
make test      # build and run tests
make gui       # build GUI binary
make app       # build Lemoniscate.app bundle
```

On Tiger with Xcode 2.x, uncomment Tiger flags in `Makefile`:

```bash
CC = gcc-4.0
CFLAGS += -mmacosx-version-min=10.4 -I/usr/local/include
OBJCFLAGS += -mmacosx-version-min=10.4 -I/usr/local/include
YAML_LDFLAGS = -L/usr/local/lib -lyaml
```

## Building (Linux)

```bash
sudo apt install build-essential libssl-dev libyaml-dev   # Debian/Ubuntu
make
```

See [docs/LINUX.md](docs/LINUX.md) for other distributions, systemd setup, and Docker.

## Dependencies

**macOS:**
- CoreFoundation, Security, CoreServices (ship with macOS)
- AppKit (for GUI only)
- pthreads
- libyaml

**Linux:**
- OpenSSL (libssl, libcrypto)
- libyaml
- pthreads

## Documentation

- [Server reference](docs/SERVER.md)
- [GUI reference](docs/GUI.md)
- [Security](docs/SECURITY.md)
- [Linux deployment](docs/LINUX.md)
- [Docker on Linux](docs/DOCKER-LINUX.md)
- [Docker on macOS](docs/DOCKER-MACOS.md)
- [Docker on Windows](docs/DOCKER-WINDOWS.md)
- [Configuration reference](config/config.yaml.example)
- [Docs index](docs/README.md)

## Related projects

- [Mobius](https://github.com/jhalter/mobius) -- The Go Hotline server that originally inspired this project

## Why "Lemoniscate"?

A lemniscate is the mathematical name for the infinity symbol -- the figure-eight curve that is closely related to the Mobius strip in topology. Both are continuous, single-surface loops. This is a stupid pun and it's an original name.


## License

See LICENSE file.
