## Why

Lemoniscate is a mature ~18K-line C/Obj-C Hotline server with comprehensive protocol coverage, but it has no formal capability specifications. As the project grows across two target branches (Tiger/PPC and modern macOS), changes risk breaking protocol compliance, permission semantics, or file transfer integrity without a spec baseline to validate against. Bootstrapping specs now establishes a contract for each major capability, enabling safer refactoring, clearer contributor onboarding, and testable acceptance criteria.

## What Changes

- Introduce OpenSpec specifications covering Lemoniscate's core capabilities
- Define behavioral contracts for the Hotline wire protocol, authentication, chat, file operations, user management, news, networking, server configuration, and the admin GUI
- No code changes — this is a documentation/specification-only change that formalizes existing behavior

## Capabilities

### New Capabilities
- `wire-protocol`: Hotline wire format — transaction framing, field encoding/decoding, handshake sequence, big-endian serialization, and protocol version negotiation (v190)
- `authentication`: User login flows including plaintext password, HOPE secure login (SHA-1 challenge-response), TLS encryption, and guest access
- `chat-messaging`: Public chat broadcast, private chat rooms, instant messages, /me actions, auto-reply, and admin broadcast
- `file-transfer`: File browsing, upload/download, resumable transfers, FILP format with resource/data fork preservation, folder operations, type/creator codes, and file aliases
- `user-management`: Account CRUD (YAML persistence), 41-bit permission system, access control enforcement, kick/ban, and batch editing
- `news-board`: Flat message board and threaded news with categories, articles, and YAML persistence
- `networking`: Tracker registration (UDP heartbeat), Bonjour/mDNS local discovery, kqueue event loop, per-IP rate limiting, and idle disconnect
- `server-config`: YAML and plist configuration loading, CLI flags, --init scaffolding, signal handling (SIGHUP reload), and agreement file serving

### Modified Capabilities
<!-- No existing specs to modify — this is a greenfield bootstrap. -->

## Impact

- **Code**: No code changes. Specs describe existing behavior in `src/hotline/`, `src/mobius/`, and `src/gui/`.
- **Testing**: Specs will provide acceptance criteria that can inform future test coverage beyond the current wire-format tests.
- **Documentation**: Complements existing `docs/SERVER.md`, `docs/GUI.md`, and `docs/FEATURE_PARITY.md` with formal behavioral contracts.
- **Workflow**: Establishes the OpenSpec workflow for future changes — all subsequent feature work can reference and extend these specs.
