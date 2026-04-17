## Why

The Mac GUI app has no controls for Mnemosyne search integration (added in 0.1.5) or text encoding selection (hardcoded to "macintosh"). Users must manually edit config.yaml to configure Mnemosyne, which defeats the purpose of the GUI. Encoding should also be user-selectable since UTF-8 clients exist.

## What Changes

- Add a **Mnemosyne Search** section to the GUI with:
  - Enable checkbox (toggles sync by clearing/restoring the URL)
  - URL text field (default: `http://tracker.vespernet.net:8980`)
  - API Key text field (visible, plain text — matches YAML storage)
  - Three index checkboxes: Files, News, Message Board (all default on)
- Add a **Text Encoding** popup control (`Mac Roman` / `UTF-8`) replacing the hardcoded `"macintosh"` value
- Persist all new fields in the plist config (read/write in `loadConfigFromDisk` / `writeConfigToDisk`)
- Pass Mnemosyne and encoding values through to the server process via config.yaml generation

## Capabilities

### New Capabilities
- `gui-mnemosyne-controls`: GUI controls for Mnemosyne sync configuration (enable, URL, API key, index toggles)
- `gui-encoding-control`: GUI control for text encoding selection (Mac Roman / UTF-8)

### Modified Capabilities

## Impact

- **Files**: `AppController.h`, `AppController+LayoutAndTabs.inc`, `AppController+LifecycleConfig.inc`, `AppController+GeneralActions.inc`
- **Dependencies**: None new — Mnemosyne and encoding are already in the server config struct
- **Risk**: Low — additive UI controls, no server-side changes needed
