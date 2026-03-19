# Lemoniscate - A modern Hotline Server for 10.4/10.5 Macs


A native C and Objective-C implementation of the Hotline protocol for Mac OS X 10.4 Tiger and 10.5 Leopard on PowerPC. This project is a ground-up port of [Mobius](https://github.com/jhalter/mobius), a modern Hotline server and client written in Go by Jeff Halter.

![Lemoniscate](docs/images/lemoniscate.png)


## What is this?

This is a from-scratch (well, Agentic) rewrite of the Mobius Hotline server and client in C and Objective-C, targeting the PowerPC Macs that Hotline was originally built for. The goal is a native, lightweight binary that runs on PPC Macs (10.4 and 10.5).

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

## Quickstart (CLI server)

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

- Implemented:
  - Core Hotline handshake/login, chat, users/accounts, flat message board, and base server lifecycle.
  - Config loading from YAML and account/ban/agreement/message board file loading.
  - Native AppKit GUI for start/stop/restart and runtime logs.
- Partial:
  - `SIGHUP` reload path exists but is not yet integrated into the active event loop.
  - Threaded news handlers exist with access checks but currently return empty responses.
  - File transfer-related handlers are present, but transfer-port data path is still stubbed.
  - GUI settings persistence is currently focused on UI defaults/process launch, not full `config.yaml` authoring.
- Planned:
  - REST API parity with Mobius `/api/v1/` style endpoints.

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
