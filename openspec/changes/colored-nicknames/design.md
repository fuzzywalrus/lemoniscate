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
  2. Else if cfg->colored_nicknames.honor_client_colors
     and c->nick_color is set (not 0) → use c->nick_color.
  3. Else if c->account class matches admin template
     and cfg->colored_nicknames.default_admin_color valid → use it.
  4. Else if c->account class matches guest template
     and cfg->colored_nicknames.default_guest_color valid → use it.
  5. Else → 0xFFFFFFFF (no color).
```

### 4. Delivery gating and client-color handling (two orthogonal axes)

The feature is controlled by two orthogonal config values:

**`delivery` — when to emit `DATA_COLOR` in outgoing transactions (fogWraith-aligned names):**

- `delivery == off` short-circuits the whole feature. No `DATA_COLOR` is parsed (incoming values are discarded) or emitted. `color_aware` remains false for all sessions. `honor_client_colors` has no effect.
- `delivery == auto` (default) emits `DATA_COLOR` only to clients that have opted in by sending `DATA_COLOR` in their own 304. This is fogWraith's recommended default and matches Hotline Navigator's implicit-opt-in expectation.
- `delivery == always` emits `DATA_COLOR` to every recipient regardless of opt-in. Non-supporting clients ignore the trailing field (fogWraith guarantees this). Useful when an operator wants to force color rollout without relying on client opt-in.

**`honor_client_colors` — whether client-sent `DATA_COLOR` values enter the cascade:**

- `false` (default): client-sent value is used only to mark the session color-aware (for `auto` delivery purposes); the value itself is discarded and cascade step 2 is skipped.
- `true`: client-sent value becomes tier 2 of the cascade. Per-account YAML still wins (tier 1).

**Resolved — Q1 from the original design.** The `server_only`/`user_choice` naming (original proposal) conflated delivery gating with input sourcing. Splitting into two knobs aligns fogWraith's canonical names where they meet our semantics (`off`/`auto`/`always`) and makes the "honor client colors" decision explicit.

**Resolved — Q2 from the original design.** In `delivery == off` mode, incoming `DATA_COLOR` on 304 is silently ignored — no side effects, no state change. Stripping from the session's cached field list was considered and rejected: there's no observable benefit since we never emit in off mode anyway.

### 5. Account class detection (Option A — exact match, shared-header constants)

`hl_account_class(account) -> enum { CLASS_ADMIN, CLASS_GUEST, CLASS_CUSTOM }` compares `account.access` bitfield against two templates defined as constants in `include/hotline/access.h`:

- `ADMIN_ACCESS_TEMPLATE` — all access bits set (mirrors GUI's `adminAccessTemplate` which returns all flags).
- `GUEST_ACCESS_TEMPLATE` — the specific subset currently hand-coded in `src/gui/AppController+AccountsData.inc:124`. Task 12 of this change migrates the GUI to reference the shared header, making `include/hotline/access.h` the single source of truth for what "admin" and "guest" mean at the access-bitmask level.

Exact bitfield equality wins. Any divergence → `CLASS_CUSTOM` → no class default color.

**Resolved — Q3 from the original design.** The task that consolidates GUI and server class detection into a shared header is now a required part of this change (task section 12), not a "follow-up". This closes the drift window: by the time this change lands, there is no separate GUI copy to diverge. A useful side effect is that `include/hotline/access.h` becomes authoritative for class identity across the whole codebase, which any future class-based feature (moderator badges, role filters, an explicit `Class` YAML field) can reuse without duplicating the permission-set constants.

### 6. YAML parsing and round-trip

- `yaml_account_manager.c` gains parse/write of the optional `Color` key.
- On parse: expects `#RRGGBB` hex string; accepts lowercase and uppercase; rejects invalid length or non-hex chars with a `log_warn` and treats as absent.
- On write: emits `Color: "#RRGGBB"` if set, omits the key entirely if not. Round-trip preserves "absent" vs. "explicitly cleared" distinction — there is no "explicit cleared" value at the YAML level, only present or absent.
- Internally, `account.nick_color` uses the same `uint32_t` encoding. `0` means "absent", `0xFFFFFFFF` means "explicitly no color" (reserved; not used by YAML layer, only by the cascade's output).

### 7. Config section

`config_loader.c` parses a new top-level `ColoredNicknames` mapping:

```yaml
ColoredNicknames:
  Delivery: auto              # off | auto | always (fogWraith canonical)
  HonorClientColors: false    # when true, client-sent DATA_COLOR enters the cascade
  DefaultAdminColor: "#FFD700"
  DefaultGuestColor: "#999999"
```

- `Delivery` defaults to `off` when the section is missing entirely — zero behavioral change for existing deployments. When the section is present but `Delivery` is absent, default is `auto`.
- `HonorClientColors` defaults to `false`.
- `DefaultAdminColor` / `DefaultGuestColor` default to "no color" (`0xFFFFFFFF`). Empty string and absent key are treated identically.
- Invalid hex → `log_warn` + treat as absent. Unknown `Delivery` value → `log_warn` + `off`. Invalid `HonorClientColors` (non-bool) → `log_warn` + `false`.

### 8. Transaction handlers

- **`TRAN_SET_CLIENT_USER_INFO` (304)**: when `delivery != off`, parse `DATA_COLOR` field if present. Set `c->color_aware = true`. If `honor_client_colors`, store value on `c->nick_color` (including `0xFFFFFFFF` to mean "explicitly no color"). If `delivery == off`, silently ignore any incoming `DATA_COLOR`.
- **`TRAN_NOTIFY_CHANGE_USER` (301)** and **`TRAN_NOTIFY_CHAT_USER_CHANGE` (117)**: compute "should emit" per receiver:
  - `delivery == off`: never emit.
  - `delivery == auto`: emit only if `receiver->color_aware` AND resolved color isn't `0xFFFFFFFF`.
  - `delivery == always`: emit if resolved color isn't `0xFFFFFFFF` (regardless of `color_aware`).
- **User self-info response** (the one returned immediately after 304): same rules as 301/117, with the receiver being the requesting client.
- **User list** (`TRAN_GET_USER_NAME_LIST` response): for each listed user, apply the same "should emit" calculation per receiver.

### 9. GUI — Account Editor

Three widgets added between the Name/File Root row and the Template popup row:

- `NSColorWell *accountColorWell` — opens the system color picker.
- `NSTextField *accountColorHexField` — shows/accepts `#RRGGBB`, stays in sync with the color well via notification targets.
- `NSButton *accountColorNoneCheckbox` — when checked, clears the color and disables the color well / hex field.

Bound to the selected account's `nick_color` on load, written back on save. "None" (checkbox checked) corresponds to absent YAML `Color:` key (cascade falls through to class defaults).

### 10. GUI — Server Settings disclosure section

A new "Colored Nicknames" disclosure section in the left settings panel, following the Mnemosyne / Encoding pattern:

- Delivery popup (Off / Auto / Always).
- "Honor client colors" checkbox.
- Default Admin Color: `NSColorWell` + hex field + None checkbox.
- Default Guest Color: same.

When delivery is `off`, the "Honor client colors" checkbox and both default-color rows are disabled (not hidden — visible-but-inert keeps the disclosure shape stable). When delivery is not `off`, all four sub-controls are enabled.

### 11. Plist persistence

The GUI writes and reads four plist keys:

- `ColoredNicknamesDelivery` — string: `off` | `auto` | `always`.
- `ColoredNicknamesHonorClientColors` — bool.
- `DefaultAdminColor` — string: `"#RRGGBB"` or `""` (empty means "no color").
- `DefaultGuestColor` — same.

Missing keys default to `off` / `false` / `""` / `""` respectively.

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

All previously-listed open questions have been resolved and folded into the Decisions section:

- **Q1** (per-account Color overrides in operator-only mode) → resolved by renaming: operator control is now explicit via `delivery` + `honor_client_colors`. See Decision 4.
- **Q2** (strip vs ignore `DATA_COLOR` in off mode) → resolved as "silently ignore". See Decision 4.
- **Q3** (GUI/server template drift) → resolved by promoting the shared-header migration (task section 12) to a required part of this change. See Decision 5.

## Risks

- **Wire format fork.** If a third-party server adopts a different `DATA_COLOR` encoding (e.g., `RGBA` instead of `0x00RRGGBB`), our implementation would be incompatible. Mitigation: we follow fogWraith's published spec exactly; other vendors will either align or fork visibly.
- **Class-detection surprise.** An admin who loses one permission bit silently loses their class default color. Mitigation: this is documented in the proposal as accepted fragility. Operators who want stable class-based colors can set a per-account YAML `Color:` as a workaround. Shared-header constants (Decision 5) prevent *drift* between GUI and server but don't prevent operator-induced permission edits from changing the computed class.
- **Plist/YAML desync during GUI save crash.** If the GUI writes plist successfully but crashes before writing YAML, the two diverge until the next GUI save. Mitigation: same risk as all existing plist-backed settings; accepted.
