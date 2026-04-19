## Context

fogWraith's `DATA_COLOR` (field `0x0500`) is a small, additive protocol extension that adds a 4-byte color value — encoded as `0x00RRGGBB` with the sentinel `0xFFFFFFFF` meaning "no color" — to selected user-info transactions. Color-aware clients announce themselves by including a `DATA_COLOR` field in their first `TRAN_SET_CLIENT_USER_INFO` (304); the server then echoes colors back in user-list notifications. Legacy clients that don't understand the field ignore it.

The proposal describes the server-side resolution cascade, three operator modes, per-account admin overrides, class defaults, and the GUI surface. This design captures the implementation-level decisions that the proposal deliberately leaves open.

Constraints shaping the decisions below:

- **Backward compatibility is mandatory.** The Hotline protocol has no version negotiation beyond capability bits. New fields must be additive and ignorable.
- **`hl_client_conn` is already wide.** Adding two fields (`nick_color`, `color_aware`) is fine but we avoid touching the struct layout outside the designated growth area.
- **Account YAML is hand-editable.** Any new key has to be optional; omitting it must behave exactly like the pre-change codebase.
- **GUI config persistence is plist-based.** Modern macOS `NSUserDefaults` reads `com.lemoniscate.server.plist` at launch; the GUI writes `config.yaml` on save via its own serializer. Color settings cross both storage layers.
- **The class detection heuristic is fragile by design (Option A).** The proposal explicitly accepts this; Option B (explicit `Class` field) is deferred to a future change. This design must handle the "no class match" case gracefully — falling through to `0xFFFFFFFF`.
- **PPC/modern source parity.** The server-side implementation (`hotline/`, `mobius/`) should be source-identical between the two codebases so backports are drop-ins. Only GUI code diverges (Tiger compat).

## Goals / Non-Goals

**Goals:**
- Wire support for `DATA_COLOR` (0x0500) in both directions: parse on 304, emit on 301 / 117 / user-self-info responses.
- Server-side color cascade: per-account override → client-sent → class default → none.
- Three operator modes (`off`, `server_only`, `user_choice`) selectable via `config.yaml` and the GUI.
- Account Editor and Server Settings GUI controls that make the feature operable without hand-editing YAML.
- Backward-compatible with legacy clients — they never see `DATA_COLOR` and experience no behavior change.

**Non-Goals:**
- Client-side (HLClient) rendering. This server ships nickname colors; third-party color-aware clients render them.
- An explicit `Class` field in account YAML. Deferred to a future `account-classes` change.
- Chat message colors or text tinting beyond the nickname itself.
- Per-channel or per-chatroom color overrides.
- Color-picker UX beyond `NSColorWell` — no recent-colors palette, no theme presets.

## Decisions

### 1. DATA_COLOR field format and handling

- Field ID: `0x0500` (defined as `HL_FIELD_USER_COLOR` in `include/hotline/types.h` alongside other field IDs).
- Payload: 4 bytes big-endian. Upper byte is always `0x00`. Lower 3 bytes are `RGB`. The sentinel `0xFFFFFFFF` means "no color" and is used to clear a color explicitly.
- Storage: `uint32_t nick_color` on `hl_client_conn` (0 by default, meaning "not set" — distinguishable from `0xFFFFFFFF` "explicitly no color" only when paired with the `color_aware` flag).
- Encoding: `htonl`/`ntohl` wrappers when (de)serializing — PPC is big-endian natively but the code must be source-identical to modern (little-endian x86), so the macros stay.

### 2. color_aware flag

- `bool color_aware` on `hl_client_conn`, default `false`.
- Set to `true` the first time a client's 304 includes a `DATA_COLOR` field (regardless of value). Once set, it persists for the session.
- Used as the gate for emitting `DATA_COLOR` in outgoing transactions — non-color-aware clients never see the field.

### 3. Resolution cascade

The proposal's 4-tier cascade, codified as a single `resolve_nick_color()` helper in `src/hotline/client_conn.c`:

```
hl_nick_color_resolve(hl_client_conn *c, hl_config *cfg) -> uint32_t

  1. If c->account has YAML `Color` set and valid → use it.
  2. Else if cfg->colored_nicknames.mode == user_choice
     and c->nick_color is set (not 0) → use c->nick_color.
  3. Else if c->account class matches admin template
     and cfg->colored_nicknames.default_admin_color valid → use it.
  4. Else if c->account class matches guest template
     and cfg->colored_nicknames.default_guest_color valid → use it.
  5. Else → 0xFFFFFFFF (no color).
```

### 4. Mode enforcement

- `mode == off` short-circuits the whole feature. No `DATA_COLOR` is ever parsed (incoming values are discarded) or emitted. `color_aware` remains false for all sessions.
- `mode == server_only` parses incoming `DATA_COLOR` only to set `color_aware`. The value is recorded on the connection but is never used (cascade step 2 is skipped).
- `mode == user_choice` runs the full cascade. Per-account YAML colors still win.

### 5. Account class detection (Option A — exact match)

`hl_account_class(account) -> enum { CLASS_ADMIN, CLASS_GUEST, CLASS_CUSTOM }` compares `account.access` bitfield against two hardcoded templates:

- `ADMIN_ACCESS_TEMPLATE` — all access bits set (mirrors GUI's `adminAccessTemplate` which returns all flags).
- `GUEST_ACCESS_TEMPLATE` — the specific subset listed in `src/gui/AppController+AccountsData.inc:124`. The server replicates this list in a single `hl_access_t` constant in `include/hotline/access.h`.

Exact bitfield equality wins. Any divergence → `CLASS_CUSTOM` → no class default color.

Template constants live in `include/hotline/access.h`, are exported, and are the single source of truth shared by the GUI-matcher migration (future work — GUI currently has its own copy).

### 6. YAML parsing and round-trip

- `yaml_account_manager.c` gains parse/write of the optional `Color` key.
- On parse: expects `#RRGGBB` hex string; accepts lowercase and uppercase; rejects invalid length or non-hex chars with a `log_warn` and treats as absent.
- On write: emits `Color: "#RRGGBB"` if set, omits the key entirely if not. Round-trip preserves "absent" vs. "explicitly cleared" distinction — there is no "explicit cleared" value at the YAML level, only present or absent.
- Internally, `account.nick_color` uses the same `uint32_t` encoding. `0` means "absent", `0xFFFFFFFF` means "explicitly no color" (reserved; not used by YAML layer, only by the cascade's output).

### 7. Config section

`config_loader.c` parses a new top-level `ColoredNicknames` mapping:

```yaml
ColoredNicknames:
  Mode: user_choice           # off | server_only | user_choice
  DefaultAdminColor: "#FFD700"
  DefaultGuestColor: "#999999"
```

- `Mode` defaults to `off` when the section is missing entirely — zero behavioral change for existing deployments.
- `DefaultAdminColor` / `DefaultGuestColor` default to "no color" (0xFFFFFFFF). Empty string and absent key are treated identically.
- Invalid hex → `log_warn` + treat as absent.

### 8. Transaction handlers

- **`TRAN_SET_CLIENT_USER_INFO` (304)**: parse `DATA_COLOR` field when `mode != off`. Set `c->color_aware = true`. If `mode == user_choice`, store value on `c->nick_color` (including `0xFFFFFFFF` to explicitly clear).
- **`TRAN_NOTIFY_CHANGE_USER` (301)** and **`TRAN_NOTIFY_CHAT_USER_CHANGE` (117)**: when the receiving client is color-aware and the resolved color isn't `0xFFFFFFFF`, append `DATA_COLOR` to the outgoing field list.
- **User self-info response** (the one returned immediately after 304): same rules as 301/117.
- **User list** (`TRAN_GET_USER_NAME_LIST` response): each entry includes `DATA_COLOR` when the receiver is color-aware and the subject's resolved color is set.

### 9. GUI — Account Editor

Three widgets added between the Name/File Root row and the Template popup row:

- `NSColorWell *accountColorWell` — opens the system color picker.
- `NSTextField *accountColorHexField` — shows/accepts `#RRGGBB`, stays in sync with the color well via notification targets.
- `NSButton *accountColorNoneCheckbox` — when checked, clears the color and disables the color well / hex field.

Bound to the selected account's `nick_color` on load, written back on save. "None" (checkbox checked) corresponds to absent YAML `Color:` key (cascade falls through to class defaults).

### 10. GUI — Server Settings disclosure section

A new "Colored Nicknames" disclosure section in the left settings panel, following the Mnemosyne / Encoding pattern:

- Mode popup (Off / Server-assigned only / User choice).
- Default Admin Color: `NSColorWell` + hex field + None checkbox.
- Default Guest Color: same.

When mode is `off`, the default-color controls are disabled (not hidden — visible-but-inert keeps the disclosure shape stable).

### 11. Plist persistence

The GUI writes and reads three plist keys (string types):

- `ColoredNicknamesMode` — `off` | `server_only` | `user_choice`.
- `DefaultAdminColor` — `"#RRGGBB"` or `""` (empty means "no color").
- `DefaultGuestColor` — same.

Missing keys default to `off` / `""` / `""`.

`loadConfigFromDisk` reads these; `writeConfigToDisk` writes them; the YAML generator translates them into the `ColoredNicknames:` section.

### 12. Source parity with PPC

The following files MUST be bit-identical between this change's modern output and the PPC sibling's:

- `include/hotline/access.h` (template constants)
- `include/hotline/types.h` (field ID)
- `src/hotline/client_conn.c` (cascade helper)
- `src/mobius/yaml_account_manager.c` (Color parse/write)
- `src/mobius/config_loader.c` (ColoredNicknames section)
- `src/mobius/transaction_handlers_clean.c` (304 parse, 301/117/list emit)

The GUI files under `src/gui/` will diverge in minor ways (deprecated-API shims on PPC for Tiger compatibility) but the IBOutlet properties and action methods match by name.

## Open Questions

- **Q1: Should a per-account Color override the cascade even in `server_only` mode?** Current cascade says yes (per-account YAML always wins). Alternative: `server_only` means "operator is fully in charge" and ignores even per-account YAML. Current design sticks with "per-account always wins" — the operator hand-edits YAML, so it's still operator control, just finer-grained. Revisit if operator feedback suggests otherwise.

- **Q2: Should `mode == off` strip `DATA_COLOR` from incoming 304 fields, or just ignore it?** Current design: silently ignore. Stripping would require rewriting the session's cached user info, which is extra work for no behavioral gain (since we never emit the field in off mode anyway).

- **Q3: Class detection — what happens when GUI template constants drift from server constants?** Single source in `include/hotline/access.h` mitigates this, but the GUI currently has its own copy in `AppController+AccountsData.inc:124`. Task 10 in the task list migrates the GUI to reference the header. Until that's done, the two must be kept in sync by convention.

## Risks

- **Wire format fork.** If a third-party server adopts a different `DATA_COLOR` encoding (e.g., `RGBA` instead of `0x00RRGGBB`), our implementation would be incompatible. Mitigation: we follow fogWraith's published spec exactly; other vendors will either align or fork visibly.
- **Class-detection surprise.** An admin who loses one permission bit silently loses their class default color. Mitigation: this is documented in the proposal as accepted fragility. Operators who want stable class-based colors can set a per-account YAML `Color:` as a workaround.
- **GUI/server template drift.** See Q3.
- **Plist/YAML desync during GUI save crash.** If the GUI writes plist successfully but crashes before writing YAML, the two diverge until the next GUI save. Mitigation: same risk as all existing plist-backed settings; accepted.
