## 1. Protocol Constants and Types

- [x] 1.1 Added `HL_FIELD_USER_COLOR = {0x05, 0x00}` to `include/hotline/types.h` after the news fields.
- [x] 1.2 Added `ADMIN_ACCESS_TEMPLATE` and `GUEST_ACCESS_TEMPLATE` to `include/hotline/access.h`. Templates match `kAccountPermissionDefs` and `guestAccessTemplate` respectively.
- [x] 1.3 Added `typedef enum hl_account_class_t { HL_CLASS_CUSTOM, HL_CLASS_GUEST, HL_CLASS_ADMIN }` to `include/hotline/access.h`.
- [x] 1.4 Added `enum hl_colored_nicknames_delivery { HL_CN_DELIVERY_OFF, _AUTO, _ALWAYS }` plus a nested `colored_nicknames` struct to `hl_config_t` in `include/hotline/config.h`.

## 2. Connection State

- [x] 2.1 Added `uint32_t nick_color` and `int color_aware` to `hl_client_conn` in `include/hotline/client_conn.h`.
- [x] 2.2 Satisfied by `calloc` in `hl_client_conn_new` — both fields zero-initialize automatically.
- [x] 2.3 No teardown work (POD fields).

## 3. Account Class Detection

- [x] 3.1 Created `src/hotline/access.c` with `hl_access_classify(const hl_access_bitmap_t)` using `memcmp` against the two templates. Added to Makefile's `HOTLINE_COMMON_SRCS`. (Signature takes bitmap directly rather than `hl_account_t *` to avoid a circular include between access.h and client_conn.h.)
- [ ] 3.2 Unit-test the three branches (admin exact match, guest exact match, anything-else → custom) in `test/test_access.c`.

## 4. Color Cascade

- [x] 4.1 Implemented `hl_nick_color_resolve(const hl_client_conn_t *, const hl_config_t *)` in `src/hotline/client_conn.c`. Tier 2 gated on `cfg->colored_nicknames.honor_client_colors`. Added `#include "hotline/config.h"` to `client_conn.h` to make the typedef visible to callers.
- [x] 4.2 Returns `0xFFFFFFFF` in fall-through cases; per-account `nick_color != 0` wins as highest priority (tier 1 uses `c->account->nick_color`, populated by upcoming task 7.2).
- [ ] 4.3 Unit-test each cascade step in `test/test_nick_color.c` (new file). Include coverage for both `honor_client_colors == true` and `== false`.

## 5. Config Surface — YAML

- [ ] 5.1 Extend `hl_config_t` in `include/hotline/config.h` with a nested `colored_nicknames` struct (`delivery` enum, `honor_client_colors` bool, `default_admin_color`, `default_guest_color`).
- [ ] 5.2 Parse the `ColoredNicknames:` section in `src/mobius/config_loader.c` with keys `Delivery`, `HonorClientColors`, `DefaultAdminColor`, `DefaultGuestColor`. Treat missing section as `delivery = off`, `honor_client_colors = false`.
- [ ] 5.3 Invalid hex values → `log_warn` + treat as absent. Unknown `Delivery` → `log_warn` + `off`. Non-bool `HonorClientColors` → `log_warn` + `false`.
- [ ] 5.4 Add defaults in `hl_config_init()`: `delivery = off`, `honor_client_colors = false`, both default colors `0xFFFFFFFF`.
- [ ] 5.5 Update `config/config.yaml.example` with a commented `ColoredNicknames:` section showing `Delivery: auto`, `HonorClientColors: false`, and example default colors.

## 6. Config Surface — Plist (GUI)

- [ ] 6.1 Add four plist keys to `src/mobius/config_plist.c`: `ColoredNicknamesDelivery` (string), `ColoredNicknamesHonorClientColors` (bool), `DefaultAdminColor` (string), `DefaultGuestColor` (string).
- [ ] 6.2 In `config_plist_read`: map `ColoredNicknamesDelivery` string to enum (empty/missing → `off`); read `ColoredNicknamesHonorClientColors` as bool (missing → `false`).
- [ ] 6.3 In `config_plist_write`: emit delivery as a string, honor-client-colors as bool, and colors as `"#RRGGBB"` (empty string when no color set).
- [ ] 6.4 Wire plist → YAML translation in the GUI's save path so the running server picks up changes.

## 7. Account YAML — Color Key

- [x] 7.1 Added `uint32_t nick_color` to `hl_account_t` in `include/hotline/client_conn.h` (done in Checkpoint 1 so the cascade's tier 1 has something to read). YAML parse/write still pending in 7.2/7.3.
- [ ] 7.2 Parse optional `Color: "#RRGGBB"` in `src/mobius/yaml_account_manager.c::yaml_account_parse`. Invalid → log + treat as absent.
- [ ] 7.3 Write `Color: "#RRGGBB"` in `yaml_account_write` when `nick_color != 0`; omit the key entirely when `nick_color == 0`.
- [ ] 7.4 Round-trip test: parse → write → parse yields identical `nick_color`, in `test/test_yaml_account.c`.

## 8. Transaction Handlers — Incoming

- [ ] 8.1 In `TRAN_SET_CLIENT_USER_INFO` (304) handler in `src/mobius/transaction_handlers_clean.c`, parse `DATA_COLOR` (0x0500) if present.
- [ ] 8.2 Set `conn->color_aware = true` when the field is present (regardless of value) and `delivery != off`.
- [ ] 8.3 When `honor_client_colors == true`, store parsed value (including `0xFFFFFFFF`) in `conn->nick_color`. Otherwise discard the value.
- [ ] 8.4 When `delivery == off`, silently ignore the entire field (no `color_aware` set, no value stored).
- [ ] 8.5 Unit-test 304 parsing across the delivery × honor-client-colors matrix (at minimum: off, auto+honor=false, auto+honor=true, always+honor=true).

## 9. Transaction Handlers — Outgoing

- [ ] 9.1 Extend user-info emission helper (the one shared by 301, 117, self-info reply, and user-list) to compute a "should emit DATA_COLOR" predicate per receiver: `delivery == always ? (resolved != 0xFFFFFFFF) : (delivery == auto && receiver->color_aware && resolved != 0xFFFFFFFF)`. `delivery == off` → never.
- [ ] 9.2 `TRAN_NOTIFY_CHANGE_USER` (301): emit per receiver using the predicate.
- [ ] 9.3 `TRAN_NOTIFY_CHAT_USER_CHANGE` (117): emit per receiver using the predicate.
- [ ] 9.4 User self-info response: emit when the predicate allows (receiver = requesting client).
- [ ] 9.5 `TRAN_GET_USER_NAME_LIST` response: for each listed user, emit `DATA_COLOR` per-entry using the predicate (requester is the receiver).
- [ ] 9.6 Integration test: in `delivery == auto` + `honor_client_colors == false`, a color-aware client and a legacy client connect simultaneously; admin changes nick; color-aware receives `DATA_COLOR`, legacy does not.
- [ ] 9.7 Integration test: in `delivery == always`, legacy client receives `DATA_COLOR` in notifications and ignores it without disconnecting (behavior per fogWraith spec).

## 10. GUI — Account Editor

- [ ] 10.1 Add `accountColorWell`, `accountColorHexField`, `accountColorNoneCheckbox` IBOutlets to `AppController.h`.
- [ ] 10.2 Lay out the three widgets in `AppController+LayoutAndTabs.inc` between the Name/File Root row and the Template popup. Match spacing/style of surrounding rows.
- [ ] 10.3 Wire color well ↔ hex field sync in `AppController+AccountsActions.inc` (both directions; avoid feedback loops with a guard flag).
- [ ] 10.4 Wire "None" checkbox: when checked, disable both widgets and clear the stored color. When unchecked, enable widgets and restore/default a color.
- [ ] 10.5 Load color from selected account in `AppController+AccountsData.inc::populateAccountEditor`. Map `0` → None checked + disabled widgets.
- [ ] 10.6 Save color to YAML on account save (existing save pipeline). Omit key when None checked.
- [ ] 10.7 Gate the entire color row on the server's `Delivery` setting. When `Delivery == off`, disable color well, hex field, AND "None" checkbox. When `Delivery` changes at runtime (via Server Settings popup), update the Account Editor widgets reactively without requiring a save. Show a tooltip or inline help indicating colors are disabled at the server level when applicable.

## 11. GUI — Server Settings Section

- [ ] 11.1 Add disclosure section "Colored Nicknames" in the left settings panel, following Mnemosyne/Encoding disclosure pattern in `AppController+LayoutAndTabs.inc`.
- [ ] 11.2 Delivery popup with three items (Off / Auto / Always); `NSPopUpButton` IBOutlet `coloredNicknamesDeliveryPopup`.
- [ ] 11.3 "Honor client colors" checkbox; `NSButton` IBOutlet `coloredNicknamesHonorClientColorsCheckbox`.
- [ ] 11.4 Default Admin Color: `NSColorWell` + hex field + None checkbox. IBOutlets `defaultAdminColorWell`, `defaultAdminColorHexField`, `defaultAdminColorNoneCheckbox`.
- [ ] 11.5 Default Guest Color: same set of widgets, IBOutlets with `defaultGuest...` names.
- [ ] 11.6 When delivery is `off`, disable (do not hide) the checkbox and both default-color row widgets. When delivery is `auto` or `always`, enable them.
- [ ] 11.7 Load from plist in `AppController+LifecycleConfig.inc::loadConfigFromDisk` (reads delivery string, honor-client-colors bool, both default color strings).
- [ ] 11.8 Save to plist in `AppController+LifecycleConfig.inc::writeConfigToDisk`.

## 12. Access template consolidation (required)

- [ ] 12.1 Replace the GUI's hardcoded `adminAccessTemplate`/`guestAccessTemplate` sets in `AppController+AccountsData.inc:124` / `:132` with values derived from the shared constants in `include/hotline/access.h`. The GUI methods may remain as `NSMutableSet` wrappers for Obj-C consumers; their content comes from the C constants.
- [ ] 12.2 Verify no GUI callsite still hardcodes permission-flag names: `grep -rn 'DownloadFile\|UploadFile\|ChangeUserName\|...' src/gui/ | grep -v access.h` returns only the wrapper methods, not other call sites.
- [ ] 12.3 Smoke test: edit an account via GUI that currently has a "custom" permission set, verify class detection in the server logs matches expectation.

## 13. Documentation

- [ ] 13.1 Update `docs/FEATURES.md` with a "Colored nicknames" section (wire field, three modes, cascade summary, GUI location).
- [ ] 13.2 Update `docs/FEATURE_PARITY.md` to list colored-nicknames as supported.
- [ ] 13.3 Add a short `docs/COLORED_NICKNAMES.md` operator guide: enabling, picking a mode, per-account vs. class defaults, compatible clients.
- [ ] 13.4 Update `README.md`'s features list.

## 14. Build and Smoke Test

- [ ] 14.1 `make clean && make` builds server and GUI with zero new warnings.
- [ ] 14.2 `make test` passes including new `test_access.c`, `test_nick_color.c`, and yaml round-trip additions.
- [ ] 14.3 Manual: connect Hotline Navigator, observe admin nickname shows configured color; connect classic Hotline client, observe no regression.
- [ ] 14.4 Manual: change admin permissions via GUI, confirm class-default color falls through as the design predicts (documents the fragility on the ground).
