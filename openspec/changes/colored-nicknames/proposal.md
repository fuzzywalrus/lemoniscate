Sibling: `/ppc/openspec/changes/colored-nicknames/` — PPC-side implementation of the same feature. Scope and wire format match; per the repo-layout spec, cross-codebase work uses coordinated sibling proposals.

## Why

Modern Hotline clients (notably Hotline Navigator) support colored nicknames via fogWraith's `DATA_COLOR` protocol extension (field 0x0500). Lemoniscate has no support for this — all nicknames are uncolored. Adding server-side color support with admin controls lets server operators assign colors by account class or per-user, giving visual identity to roles (gold for admins, gray for guests, etc.) and enabling compatibility with color-aware clients.

**Reference**: [fogWraith Colored Nicknames spec](https://github.com/fogWraith/Hotline/blob/main/Docs/Protocol/Colored-Nicknames.md)

## What Changes

### Server-side protocol support

- Define `DATA_COLOR` field (0x0500) — a 4-byte `0x00RRGGBB` value, with `0xFFFFFFFF` meaning "no color."
- Add `nick_color` (uint32) and `color_aware` (bool) fields to `hl_client_conn`.
- Parse `DATA_COLOR` from `TRAN_SET_CLIENT_USER_INFO` (304) — when received, mark the session as color-aware.
- Include `DATA_COLOR` in outgoing transactions when applicable:
  - `TRAN_NOTIFY_CHANGE_USER` (301)
  - `TRAN_NOTIFY_CHAT_USER_CHANGE` (117)
  - User self-info responses
- Only send `DATA_COLOR` to color-aware clients (implicit opt-in per the spec).

### Color resolution cascade

Resolve the effective nick color using this priority:

1. **Per-account admin-assigned color** (from `Color` key in account YAML) — always wins
2. **Client-sent color** (only in "User choice" mode, and only if no admin color is set)
3. **Account class default** (from `config.yaml` — admin template or guest template match)
4. **No color** (`0xFFFFFFFF`)

### Three server modes

| Mode | Config value | Behavior |
|------|-------------|----------|
| Off | `off` | Strip all color data; never send `DATA_COLOR` |
| Server-assigned only | `server_only` | Admin/class colors are sent; client-sent colors are ignored |
| User choice | `user_choice` | Users may pick colors; admin overrides still win |

### Account YAML extension

Add an optional `Color` key to account YAML files:

```yaml
Login: admin
Name: admin
Password: "$2b$..."
Color: "#FFD700"
Access:
  DownloadFile: true
  ...
```

When `Color` is absent or empty, the account has no admin-assigned color (falls through the cascade).

### Server config extension

Add a `ColoredNicknames` section to `config.yaml`:

```yaml
ColoredNicknames:
  Mode: user_choice
  DefaultAdminColor: "#FFD700"
  DefaultGuestColor: "#999999"
```

### Account class detection (Option A)

Determine account class by comparing the account's access permission set against the known templates:

- If permissions match the admin template exactly -> admin class
- If permissions match the guest template exactly -> guest class
- Otherwise -> custom (no class default applies)

**Future direction (Option B)**: Add an explicit `Class` field to account YAML, making the template popup a stored role rather than a one-time shortcut. This would enable richer role-based defaults beyond just admin/guest and decouple class identity from exact permission matching. See "Future: Account Classes" below.

### GUI — Account Editor

Add a color control row to the account editor (between Name/File Root and the Template popup):

- **NSColorWell** — opens the macOS system color picker
- **Hex text field** — displays/accepts `#RRGGBB`, stays synced with the color well
- **"None" checkbox** — clears the color (meaning "no admin override, fall through to defaults")

### GUI — Server Settings

Add a "Colored Nicknames" disclosure section to the left settings panel:

- **Mode popup** — Off / Server-assigned only / User choice
- **Default admin color** — NSColorWell + hex field + None checkbox
- **Default guest color** — NSColorWell + hex field + None checkbox

Default color controls are only visible/enabled when mode is not "Off."

### GUI — Plist persistence

Store the colored nickname settings in the GUI's plist config (`com.lemoniscate.server.plist`):

- `ColoredNicknamesMode` (string: `off`, `server_only`, `user_choice`)
- `DefaultAdminColor` (string: hex or empty)
- `DefaultGuestColor` (string: hex or empty)

Read in `loadConfigFromDisk`, write in `writeConfigToDisk`, pass through to `config.yaml` generation.

## Capabilities

### New Capabilities
- `colored-nicknames`: Server-side DATA_COLOR (0x0500) protocol support — field parsing, color cascade resolution, mode enforcement, and inclusion in user notification transactions.
- `gui-color-controls`: GUI controls for colored nickname administration — per-account color assignment in the account editor, server-wide mode and class default colors in the settings panel.

### Modified Capabilities
- `user-management`: Account YAML gains an optional `Color` field. User change notifications (301, 117) gain an optional `DATA_COLOR` trailing field. User list entries may include color data.
- `server-config`: `config.yaml` gains a `ColoredNicknames` section with `Mode`, `DefaultAdminColor`, and `DefaultGuestColor` keys.

## Impact

- **Server code**: `client_conn.h` (new fields), `server.c` (color cascade logic, DATA_COLOR in notifications), `transaction_handlers_clean.c` (parse DATA_COLOR from 304, include in user list), `types.h` (new field ID constant), `yaml_account_manager.c` (parse/write Color key), `config_loader.c` (parse ColoredNicknames section).
- **GUI code**: `AppController.h` (new IBOutlet properties), `AppController+LayoutAndTabs.inc` (color controls in account editor and settings panel), `AppController+LifecycleConfig.inc` (plist read/write for color settings), `AppController+AccountsData.inc` (load/populate color from account YAML), `AppController+AccountsActions.inc` (save color to account YAML).
- **Wire format**: Transactions 301, 117, 304, and user self-info gain an optional trailing `DATA_COLOR` field. Backward compatible — non-supporting clients ignore the extra field.
- **Dependencies**: None new. NSColorWell is available since macOS 10.0 (no Tiger compatibility concern).
- **Risk**: Low-medium. Protocol change is additive and backward-compatible. GUI changes follow the established pattern (Mnemosyne/encoding controls). The class detection heuristic (Option A) is slightly fragile — accounts with custom permissions that happen to match a template exactly will get a class default color unexpectedly, but this is an edge case.

## Future: Account Classes (Option B)

The current template system (Admin/Guest/Custom) is stateless — the template popup is a shortcut that applies a permission set, but the "class" is not persisted. This means class detection for color defaults relies on exact permission matching, which is fragile.

A future change could introduce an explicit `Class` field in account YAML:

```yaml
Login: admin
Name: admin
Class: admin
Color: "#FFD700"
Access:
  ...
```

This would:
- Make the Template popup a stored role selector
- Enable per-class color defaults beyond admin/guest (e.g., moderator, bot)
- Decouple class identity from permission sets (a custom-permissioned account can still be "admin" class)
- Support future role-based features beyond just colors

This is a larger data model change and should be its own proposal when the need arises.
