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

- `include/hotline/` and `src/hotline/` -- The core protocol library. Wire format parsing, serialization, handshake, user and access types, text encoding, and the Objective-C client. This compiles into `libhotline.a`.
- `include/mobius/` and `src/mobius/` -- The server application. Transaction handlers, YAML-based persistence (accounts, bans, threaded news), configuration loading, and the REST API. This is where the business logic lives.

Each Go source file has a corresponding C or Objective-C file. A mapping table is maintained in the [plan document](.claude/plans/mighty-tickling-feather.md) and will eventually live in a standalone MAPPING.md.

## What works today

- TBA

## Building

On modern macOS for development and testing:

```
make test      # build and run all tests
make all       # build libhotline.a
```

On Tiger with Xcode 2.x, uncomment the Tiger flags in the Makefile:

```
CC = gcc-4.0
CFLAGS += -mmacosx-version-min=10.4
```

Dependencies:
- CoreFoundation (ships with Tiger)
- Foundation (ships with Tiger)
- pthreads (ships with Tiger)
- libyaml (via Tigerbrew, needed for Phase 4+)

## Related projects

- [Mobius](https://github.com/jhalter/mobius) -- The Go implementation this project is based on
- [mobius-macOS-GUI](https://github.com/fuzzywalrus/mobius-macOS-GUI) -- A SwiftUI wrapper around Mobius for modern macOS

## Why "Lemoniscate"?

A lemniscate is the mathematical name for the infinity symbol -- the figure-eight curve that is closely related to the Mobius strip in topology. Both are continuous, single-surface loops. This is a stupid pun and it's an original name.


## License

This project is a clean-room implementation referencing the Mobius source code. See the Mobius repository for its licensing terms.
