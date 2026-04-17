# Lemoniscate GUI Reference

## Overview

`Lemoniscate.app` is a native AppKit admin frontend for `lemoniscate`.
It starts, stops, and restarts the server process and streams runtime logs.

## Build

From the repository root:

```bash
make all
make gui
make app
```

For a Leopard PowerPC host that needs a PPC + Intel universal app targeting PowerPC 10.4/10.5 and Intel 10.6:

```bash
make universal-app \
  UNIVERSAL_I386_SDKROOT=/Developer/SDKs/MacOSX10.4u.sdk \
  UNIVERSAL_I386_YAML_LDFLAGS=/path/to/i386-or-universal/libyaml.a
```

This produces:
- `lemoniscate` (server binary)
- `lemoniscate-gui` (GUI executable)
- `Lemoniscate.app` (app bundle)

## Run

### Launch app bundle

```bash
open Lemoniscate.app
```

### Launch GUI binary directly (development)

```bash
./lemoniscate-gui
```

## App bundle layout

Expected bundle contents:

```text
Lemoniscate.app/
  Contents/
    Info.plist
    MacOS/
      Lemoniscate
      lemoniscate-server
```

At runtime, the GUI looks for `lemoniscate-server` in:
1. The app bundle at `Contents/MacOS/lemoniscate-server`
2. As a sibling binary next to `Lemoniscate.app` (same name: `lemoniscate-server`)

## Architecture compatibility

`make app` validates that `lemoniscate` and `lemoniscate-gui` contain the same architecture slice set before packaging.

If architectures do not match, packaging fails with a clear error.
You can bypass this with:

```bash
APP_SKIP_ARCH_CHECK=1 make app
```

Use bypass only for intentional advanced cases.

The current Intel target is Mac OS X 10.6 Snow Leopard. Mac OS X 10.7 Lion is not supported because of the `libcrypto` dependency.

## How GUI launches server

When you click Start, the GUI invokes:

```text
lemoniscate-server --config <configDir> --bind <port> --log-level info
```

`--bind` is accepted by the server as an alias for `--port`.

## Current limitations

- The GUI persists selected UI values in `NSUserDefaults`; it is not yet a full editor for writing all `config.yaml` settings.
- If the config directory does not exist, GUI creates the directory, but it does not generate full default scaffolding (`Users`, `config.yaml`, `Banlist.yaml`, etc.).
- For a clean first-time setup, initialize config first:

```bash
./lemoniscate --init -c ./config
```

Then point the GUI at that directory.

## Troubleshooting

- "Server binary not found"
  - Ensure `lemoniscate-server` exists in `Lemoniscate.app/Contents/MacOS/`.
  - Or place `lemoniscate-server` next to the app bundle for fallback detection.
- "Architecture mismatch" during `make app`
  - Rebuild both `lemoniscate` and `lemoniscate-gui` with the same slice set (`ppc`, `i386`, or both).
- App launches on Intel 10.6 but not 10.7
  - 10.6 Snow Leopard is the current Intel target. 10.7 Lion is not supported because of the `libcrypto` dependency.
- Server fails to start from GUI
  - Confirm the selected config directory exists and contains valid files.
  - If this is first run, create defaults with `./lemoniscate --init -c <dir>`.
  - Verify the selected port is free.
- Logs tab appears quiet
  - Start the server first; logs are streamed from stdout/stderr only while process is running.

## See also

- [Server reference](SERVER.md)
- [Docs index](README.md)
