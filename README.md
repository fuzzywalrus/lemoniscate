# Lemoniscate - A modern Hotline Server for 10.4/10.5 Macs, macOS 10.11 - Current and Linux


A native C and Objective-C implementation of the 1.9 Hotline protocol with extended modern features for Mac OS X 10.4 Tiger and 10.5 Leopard on PowerPC and macOS 10.11 with a GUI and setup wizard.  The Linux version is CLI only.  This project is a ground-up port of [Mobius](https://github.com/jhalter/mobius), a modern Hotline server and client written in Go by Jeff Halter.

![Lemoniscate](docs/images/lemoniscate-256.png)


## What is this?

![Lemoniscate](https://hotlinenavigator.com/lemoniscate-screenshot.png)


This started as a from-scratch (well, Agentic) rewrite of the Mobius Hotline server and client in C and Objective-C, targeting the PowerPC Macs that Hotline was originally built for. The goal is a native, lightweight binary that runs on PPC Macs (10.4 and 10.5).  

Since then it has evolved into an advanced Hotline Server, meant to be user friendly for regular people with a setup wizard, ability to create TLS certificates for encryption with a click click of the mouse, and sports support for modern features, like HOPE encrypted logins for pure end-to-end encryption and mnemosyne search support. 

With PowerPC OS X as the original target, Lemoniscate is extremely lightweight. Using generally under 50~ MB of RAM on both retro and modern systems. 

![Lemoniscate](https://hotlinenavigator.com/lemoniscate-modern-screenshot.png)


See the releases for the latest version!

## Why not just fork Mobius?

Mobius is written in Go. Go ironically does have PowerPC support but it never supported Mac OS X 10.4 or 10.5, and it dropped PowerPC support a long time ago. There is no way to compile Go code for Tiger or Leopard on PPC. 

Instead, this project uses Mobius as a reference implementation. The C code is structured to mirror the Go source files one-to-one, so when Mobius adds features or fixes bugs upstream, those changes can be traced directly to the corresponding C files here. It is a spiritual port, not a fork.

## What is Hotline?

Hotline was a client-server platform from the late 1990s that combined file sharing, group chat, instant messaging, and a news bulletin board into a single application. It was especially popular on the Mac and had a devoted community through the early 2000s. Hotline servers were run by individuals and small communities, each with their own files, chat rooms, and culture. Think of it as a self-hosted Discord from 1997, minus the voice chat and plus a built-in file server.

The Hotline protocol is a binary, big-endian, TCP-based protocol. This is actually a nice fit for PowerPC, which is also big-endian -- the wire format maps directly to native memory layout with no byte-swapping required.

## Project structure

The codebase is split into two layers that mirror the Go package structure:

- `include/hotline/` and `src/hotline/` -- Core protocol library: wire format parsing, serialization, handshake, user/access types, text encoding, and the Objective-C client. Builds into `libhotline.a`.
- `include/mobius/` and `src/mobius/` -- Server application: transaction handlers, YAML-based persistence (accounts, bans, threaded news), configuration loading, and runtime server behavior.
- `src/gui/` -- Native AppKit admin GUI (`lemoniscate-gui`) that launches and supervises `lemoniscate`.
- `docs/` -- Operator and implementation documentation.
- [`modern branch`](https://github.com/fuzzywalrus/lemoniscate/tree/modern) - Currently where the modern branhc lives


## Feature status

### Chat & Messaging

- Public chat room with all connected users
- Private chat rooms — create, invite users, set topics
- Instant messages (private messages between users)
- Auto-reply when a user is away or idle
- /me action messages
- Admin broadcast messages to all users

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

### Server Administration

- GUI admin application (Cocoa, native Tiger look)
- Setup wizard for first-time configuration
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

- Runs on Mac OS X 10.4 Tiger (PowerPC)
- Works with Hotline Navigator, the mierau Swift client, and classic Hotline clients
- FILP file transfer format with INFO and DATA forks
- Hotline 1.8+ protocol (version 190)
- CLI server binary for headless operation
- Mobius-compatible account and news file formats


## Building

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

Dependencies:
- CoreFoundation (ships with Tiger)
- Foundation (ships with Tiger)
- AppKit (for GUI)
- pthreads (ships with Tiger)
- libyaml (via Tigerbrew or source build)


## Building for non-macOS/ OS X - Quickstart to rolling your own (CLI server)

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


## Documentation

- [Server reference](docs/SERVER.md)
- [GUI reference](docs/GUI.md)
- [Docs index](docs/README.md)

## Related projects

- [Mobius](https://github.com/jhalter/mobius) -- The Go implementation this project is based on
- [mobius-macOS-GUI](https://github.com/fuzzywalrus/mobius-macOS-GUI) -- A SwiftUI wrapper around Mobius for modern macOS

## Why "Lemoniscate"?

A lemniscate is the mathematical name for the infinity symbol -- the figure-eight curve that is closely related to the Mobius strip in topology. Both are continuous, single-surface loops. This is a stupid pun and it's an original name.


## License

This project is a clean-room implementation referencing the Mobius source code. See the Mobius repository for its licensing terms.
