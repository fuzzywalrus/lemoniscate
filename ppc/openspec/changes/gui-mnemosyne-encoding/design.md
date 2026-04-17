## Context

The Lemoniscate Mac GUI uses a disclosure-section pattern for config (HOPE, TLS, Limits, etc.). Each section has a collapsible header and content view built programmatically in `AppController+LayoutAndTabs.inc`. Config is persisted as a macOS plist (`~/Library/Preferences/com.lemoniscate.server.plist`) and loaded via `config_plist.c` into `hl_config_t`.

Mnemosyne sync (added in 0.1.5) has 5 config fields with no GUI controls. Encoding is hardcoded to `"macintosh"` with no user control.

### Data flow

```
GUI controls → writeConfigToDisk() → plist file
                                         ↓
                          config_plist.c reads plist → hl_config_t
                                                          ↓
                                                   server uses config
```

## Goals / Non-Goals

**Goals:**
- Add Mnemosyne section with enable toggle, URL, API key, and 3 index checkboxes
- Add Encoding popup (Mac Roman / UTF-8) replacing the hardcoded value
- Persist all new fields through the plist read/write path
- Follow existing GUI patterns (disclosure sections, helper functions, help buttons)

**Non-Goals:**
- Server-side config changes (already done)
- Mnemosyne registration flow in the GUI (users register at agora.vespernet.net)
- Validating the API key format or testing connectivity

## Decisions

### 1. Mnemosyne enable toggle clears/restores URL

The server checks `mnemosyne_url[0] != '\0'` to decide if sync is enabled. The GUI enable checkbox will:
- When unchecked: store URL in a `_mnemosyneSavedURL` ivar, clear the field
- When checked: restore the saved URL (or default to `http://tracker.vespernet.net:8980`)

**Why not a separate enable bool?** Adds a config field the server doesn't use. Keeping the "URL empty = disabled" contract is simpler.

### 2. Section placement: Mnemosyne after TLS, Encoding in General

Mnemosyne gets its own disclosure section after TLS (it's a network feature). Encoding goes in the existing General section as a popup alongside the existing fields.

**Section order:**
```
General (+ Encoding popup added here)
Bonjour
Tracker Registration
Files
News
HOPE
TLS Encryption
Mnemosyne Search    ← NEW section
Limits
Monitoring
```

### 3. Plist keys match config.yaml keys

| Plist Key | Type | Maps to |
|-----------|------|---------|
| `MnemosyneURL` | string | `cfg->mnemosyne_url` |
| `MnemosyneAPIKey` | string | `cfg->mnemosyne_api_key` |
| `MnemosyneIndexFiles` | bool | `cfg->mnemosyne_index_files` |
| `MnemosyneIndexNews` | bool | `cfg->mnemosyne_index_news` |
| `MnemosyneIndexMsgboard` | bool | `cfg->mnemosyne_index_msgboard` |
| `Encoding` | string | `cfg->encoding` (already exists in plist reader, just hardcoded in writer) |

### 4. Encoding popup values

| Display | Config value |
|---------|-------------|
| Mac Roman | `macintosh` |
| UTF-8 | `utf-8` |

Default: Mac Roman (preserves current behavior for PPC/classic Hotline clients).

## Risks / Trade-offs

- **[Risk] Users paste invalid API keys** → No mitigation needed; server already validates on first heartbeat and logs warnings. GUI doesn't need to pre-validate.
- **[Risk] Encoding change mid-session** → Config only takes effect on server restart, which the GUI already handles (stop/start cycle). No hot-reload risk.
- **[Trade-off] No connectivity test button** → Keeps scope small. Users can verify via the server log tab which already shows Mnemosyne status.
