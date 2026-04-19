Sibling: `/openspec/changes/colored-nicknames/` — modern-side implementation. Wire format and operator semantics are identical; per the repo-layout spec, cross-codebase work uses coordinated sibling proposals.

## Why

Modern Hotline clients (notably Hotline Navigator) support colored nicknames via fogWraith's `DATA_COLOR` protocol extension (field 0x0500). Lemoniscate's PPC Tiger/Leopard server has no support for this — all nicknames are uncolored. Adding server-side color support with admin controls lets server operators assign colors by account class or per-user, giving visual identity to roles (gold for admins, gray for guests, etc.) and enabling compatibility with color-aware clients. Keeping PPC at parity with the modern build protects the "both codebases on main, backports land in one commit" workflow established by the April 2026 repo flip.

**Reference**: [fogWraith Colored Nicknames spec](https://github.com/fogWraith/Hotline/blob/main/Docs/Protocol/Colored-Nicknames.md)

## What Changes

All user-visible behavior matches the modern sibling proposal. This section lists only PPC-specific deviations from that scope.

### PPC-specific constraints

- **Tiger/Leopard SDK.** The GUI target builds against the 10.4 SDK via `gcc-4.0`. `NSColorWell` has been available since macOS 10.0, so the Account Editor color picker and Server Settings color wells work natively on Tiger with no compatibility shim.
- **Big-endian native.** PPC is big-endian; the `DATA_COLOR` wire payload (`0x00RRGGBB` big-endian) maps directly to in-memory layout with no byte-swap required. `htonl`/`ntohl` macros stay in the code for source-parity with modern (which is little-endian x86).
- **No Cocoa Preferences panel.** Tiger lacks the unified preferences UI the modern build uses. The "Colored Nicknames" disclosure section slots into the same left-panel layout used by Mnemosyne/Encoding — the pattern already ships on PPC 0.1.7.
- **Source parity requirement.** The files under `ppc/src/hotline/` and `ppc/src/mobius/` that implement the wire protocol and config parsing MUST be byte-identical with their `/src/hotline/` and `/src/mobius/` counterparts in the modern sibling. The single-commit backport rule (repo-layout spec) enforces this by construction when a PR edits both paths in lockstep.

### Scope that carries over unchanged from the modern sibling

The following are described in full in the modern sibling's `proposal.md`:

- `DATA_COLOR` field (0x0500) definition and 4-byte payload format.
- `nick_color` and `color_aware` fields on `hl_client_conn`.
- Parse from `TRAN_SET_CLIENT_USER_INFO` (304), emit in `TRAN_NOTIFY_CHANGE_USER` (301), `TRAN_NOTIFY_CHAT_USER_CHANGE` (117), user self-info reply, and `TRAN_GET_USER_NAME_LIST` entries.
- Four-tier color resolution cascade (per-account YAML → client-sent when `HonorClientColors` → class default → none).
- Two-axis configuration: `Delivery` (`off` / `auto` / `always`, fogWraith canonical names) gating output, and `HonorClientColors` (bool) gating input sourcing.
- Account YAML `Color` key.
- `ColoredNicknames:` config section with `Delivery`, `HonorClientColors`, `DefaultAdminColor`, `DefaultGuestColor`.
- Account class detection via exact permission-set match against admin and guest templates (Option A). Known fragility is accepted.
- GUI Account Editor color row (color well + hex field + None checkbox).
- GUI Server Settings disclosure section.
- GUI plist persistence under `com.lemoniscate.server.plist`.

## Capabilities

### New Capabilities
- `colored-nicknames`: Server-side DATA_COLOR (0x0500) protocol support — field parsing, color cascade resolution, mode enforcement, and inclusion in user notification transactions.
- `gui-color-controls`: GUI controls for colored nickname administration — per-account color assignment in the account editor, server-wide mode and class default colors in the settings panel.

### Modified Capabilities
- `user-management`: Account YAML gains an optional `Color` field. User change notifications (301, 117) gain an optional `DATA_COLOR` trailing field. User list entries may include color data.
- `server-config`: `config.yaml` gains a `ColoredNicknames` section with `Delivery`, `HonorClientColors`, `DefaultAdminColor`, and `DefaultGuestColor` keys.

## Impact

Same shape as the modern sibling, with paths prefixed by `ppc/`:

- **Server code**: `ppc/include/hotline/client_conn.h`, `ppc/include/hotline/types.h`, `ppc/include/hotline/access.h`, `ppc/src/hotline/client_conn.c`, `ppc/src/hotline/access.c` (new), `ppc/src/mobius/config_loader.c`, `ppc/src/mobius/yaml_account_manager.c`, `ppc/src/mobius/transaction_handlers_clean.c`.
- **GUI code**: `ppc/src/gui/AppController.h`, `ppc/src/gui/AppController+LayoutAndTabs.inc`, `ppc/src/gui/AppController+AccountsData.inc`, `ppc/src/gui/AppController+AccountsActions.inc`, `ppc/src/gui/AppController+LifecycleConfig.inc`.
- **Wire format**: identical to modern — transactions 301, 117, 304, and user self-info gain an optional trailing `DATA_COLOR` field. Backward compatible on the wire; no protocol version bump.
- **Dependencies**: none new. Tiger SDK suffices.
- **Build**: `ppc/Makefile` gains the new `access.c` source. PPC GCC 4.0 C99 compilation should succeed without warnings since no new language features or headers are introduced.
- **Risk**: low. Identical to modern sibling's risk profile.

## Future: Account Classes (Option B)

Same deferral as the modern sibling. Explicit `Class` field in account YAML is its own sibling-pair proposal when operator demand emerges.
