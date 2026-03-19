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

At runtime, the GUI looks for `lemoniscate-server` in this order:
1. Inside the app bundle (`Contents/MacOS/lemoniscate-server`)
2. Next to the app bundle as a sibling fallback

## Architecture compatibility

`make app` validates that `lemoniscate` and `lemoniscate-gui` target the same CPU architecture before packaging.

If architectures do not match, packaging fails with a clear error.
You can bypass this with:

```bash
APP_SKIP_ARCH_CHECK=1 make app
```

Use bypass only for intentional advanced cases.

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
  - Rebuild both `lemoniscate` and `lemoniscate-gui` on the same target architecture/toolchain.
- Server fails to start from GUI
  - Confirm the selected config directory exists and contains valid files.
  - If this is first run, create defaults with `./lemoniscate --init -c <dir>`.
  - Verify the selected port is free.
- Logs tab appears quiet
  - Start the server first; logs are streamed from stdout/stderr only while process is running.

## See also

- [Server reference](SERVER.md)
- [Docs index](README.md)
