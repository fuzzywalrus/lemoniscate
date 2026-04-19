## 1. Protocol Constants and Types

- [ ] 1.1 Add `HL_FIELD_USER_COLOR` (0x0500) to `ppc/include/hotline/types.h` next to other field ID constants.
- [ ] 1.2 Add `ADMIN_ACCESS_TEMPLATE` and `GUEST_ACCESS_TEMPLATE` constants to `ppc/include/hotline/access.h`. Match the GUI's hand-coded sets exactly.
- [ ] 1.3 Add `enum hl_account_class { HL_CLASS_ADMIN, HL_CLASS_GUEST, HL_CLASS_CUSTOM }` to `ppc/include/hotline/access.h`.
- [ ] 1.4 Add `enum hl_colored_nicknames_mode { HL_CN_OFF, HL_CN_SERVER_ONLY, HL_CN_USER_CHOICE }` to `ppc/include/hotline/config.h`.

## 2. Connection State

- [ ] 2.1 Add `uint32_t nick_color` and `bool color_aware` to `hl_client_conn` in `ppc/include/hotline/client_conn.h`.
- [ ] 2.2 Initialize both to `0` / `false` in `hl_client_conn_init()`.

## 3. Account Class Detection

- [ ] 3.1 Implement `hl_account_class_t hl_account_class(const hl_account_t *a)` in `ppc/src/hotline/access.c` (new file). Exact `==` compare of `a->access` against each template.
- [ ] 3.2 Unit-test the three branches (admin match, guest match, custom fallthrough) in `ppc/test/test_access.c` (new file).

## 4. Color Cascade

- [ ] 4.1 Implement `uint32_t hl_nick_color_resolve(const hl_client_conn *c, const hl_config_t *cfg)` in `ppc/src/hotline/client_conn.c`.
- [ ] 4.2 Cascade order per modern design (per-account YAML → client-sent in user_choice → admin default → guest default → 0xFFFFFFFF).
- [ ] 4.3 Unit-test each cascade step in `ppc/test/test_nick_color.c` (new file).

## 5. Config Surface — YAML

- [ ] 5.1 Extend `hl_config_t` in `ppc/include/hotline/config.h` with the `colored_nicknames` struct.
- [ ] 5.2 Parse `ColoredNicknames:` section in `ppc/src/mobius/config_loader.c`. Missing section → `mode = off`.
- [ ] 5.3 Invalid hex → `log_warn` + absent. Unknown mode → `log_warn` + `off`.
- [ ] 5.4 Defaults in `hl_config_init()`: mode `off`, both colors `0xFFFFFFFF`.
- [ ] 5.5 Update `ppc/config/config.yaml.example` with a commented `ColoredNicknames:` section.

## 6. Config Surface — Plist (GUI)

- [ ] 6.1 Add plist keys `ColoredNicknamesMode`, `DefaultAdminColor`, `DefaultGuestColor` to `ppc/src/mobius/config_plist.c`.
- [ ] 6.2 `config_plist_read`: empty/missing mode → `off`; empty color → `0xFFFFFFFF`.
- [ ] 6.3 `config_plist_write`: emit mode string and `"#RRGGBB"` (or empty string for no-color).
- [ ] 6.4 Wire plist → YAML translation in the GUI save path.

## 7. Account YAML — Color Key

- [ ] 7.1 Add `uint32_t nick_color` to `hl_account_t` in the account struct header.
- [ ] 7.2 Parse `Color: "#RRGGBB"` in `ppc/src/mobius/yaml_account_manager.c::yaml_account_parse`. Invalid → log + absent.
- [ ] 7.3 Write `Color: "#RRGGBB"` in `yaml_account_write` when set; omit key when `nick_color == 0`.
- [ ] 7.4 Round-trip test in `ppc/test/test_yaml_account.c` (or extend existing file).

## 8. Transaction Handlers — Incoming

- [ ] 8.1 In `TRAN_SET_CLIENT_USER_INFO` (304) handler in `ppc/src/mobius/transaction_handlers_clean.c`, parse `DATA_COLOR` (0x0500).
- [ ] 8.2 Set `conn->color_aware = true` on presence (any mode except `off`).
- [ ] 8.3 In `user_choice` mode, store value in `conn->nick_color`. Otherwise discard.
- [ ] 8.4 Unit-test 304 parsing across the three modes.

## 9. Transaction Handlers — Outgoing

- [ ] 9.1 Extend the user-info emission helper to append `DATA_COLOR` when `receiver->color_aware && resolved_color != 0xFFFFFFFF`.
- [ ] 9.2 `TRAN_NOTIFY_CHANGE_USER` (301): emit per receiver.
- [ ] 9.3 `TRAN_NOTIFY_CHAT_USER_CHANGE` (117): emit per receiver.
- [ ] 9.4 User self-info response.
- [ ] 9.5 `TRAN_GET_USER_NAME_LIST`: per entry, per receiver.
- [ ] 9.6 Integration test on PPC: color-aware client (Hotline Navigator) and legacy client (original Hotline Client) connect simultaneously; verify each sees the wire format it expects.

## 10. GUI — Account Editor

- [ ] 10.1 Add `accountColorWell`, `accountColorHexField`, `accountColorNoneCheckbox` IBOutlets to `ppc/src/gui/AppController.h`.
- [ ] 10.2 Lay out the three widgets between Name/File Root row and Template popup in `ppc/src/gui/AppController+LayoutAndTabs.inc`.
- [ ] 10.3 Wire color well ↔ hex field sync in `ppc/src/gui/AppController+AccountsActions.inc`.
- [ ] 10.4 "None" checkbox disables widgets and clears stored color.
- [ ] 10.5 Load/populate from account in `ppc/src/gui/AppController+AccountsData.inc::populateAccountEditor`.
- [ ] 10.6 Save on the existing save pipeline. Omit `Color:` key when None.

## 11. GUI — Server Settings Section

- [ ] 11.1 Add "Colored Nicknames" disclosure section in `ppc/src/gui/AppController+LayoutAndTabs.inc` matching the Mnemosyne/Encoding pattern.
- [ ] 11.2 Mode popup (3 items); IBOutlet `coloredNicknamesModePopup`.
- [ ] 11.3 Default Admin Color row (color well + hex field + None checkbox).
- [ ] 11.4 Default Guest Color row (same pattern).
- [ ] 11.5 Disable default-color controls when mode is `off`.
- [ ] 11.6 Load from plist in `ppc/src/gui/AppController+LifecycleConfig.inc::loadConfigFromDisk`.
- [ ] 11.7 Save to plist in `writeConfigToDisk`.

## 12. GUI — Template Popup Reconciliation (follow-up)

- [ ] 12.1 Replace hand-coded `adminAccessTemplate` / `guestAccessTemplate` in `ppc/src/gui/AppController+AccountsData.inc` with constants from `ppc/include/hotline/access.h`.

## 13. Documentation

- [ ] 13.1 Update `ppc/docs/FEATURES.md` with a "Colored nicknames" section.
- [ ] 13.2 Update `ppc/docs/FEATURE_PARITY.md` to list colored-nicknames.
- [ ] 13.3 Mirror the operator guide (`ppc/docs/COLORED_NICKNAMES.md`) to match modern's version.
- [ ] 13.4 Update `ppc/README.md` features list if it enumerates them.

## 14. Build and Smoke Test on PPC G4

- [ ] 14.1 `cd ppc && make clean && make` on the G4 — builds server and GUI with zero new warnings under gcc-4.0.
- [ ] 14.2 `make test` runs new `test_access`, `test_nick_color`, and yaml round-trip additions.
- [ ] 14.3 Manual on Tiger 10.4: connect Hotline Navigator, verify admin nickname color renders. Connect classic Hotline client, verify no regression.
- [ ] 14.4 Visual check of Aqua color well rendering under Tiger theme. Document any cosmetic quirks.

## 15. Source Parity Check

- [ ] 15.1 After implementation, `diff -r src/hotline ppc/src/hotline` and `diff -r src/mobius ppc/src/mobius` show zero differences in the files touched by this change (excluding build artifacts).
- [ ] 15.2 If any divergence exists, it is either documented in the PPC sibling's design (PPC-specific constraint) or fixed.
