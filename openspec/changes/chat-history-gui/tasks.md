# Tasks — Chat History GUI

## 1. Section scaffolding

- [x] 1.1 Add ivars to `src/gui/AppController.h` for all chat-history widgets: `_chatHistoryEnabledCheckbox`, `_chatHistoryMaxMsgsField`, `_chatHistoryMaxDaysField`, `_chatHistoryLegacyBroadcastCheckbox`, `_chatHistoryLegacyCountField`, `_chatHistoryLogJoinsCheckbox`, `_chatHistoryKeyPathField`, `_chooseChatHistoryKeyButton`, `_generateChatHistoryKeyButton`, `_chatHistoryRateCapacityField`, `_chatHistoryRateRefillField`. Place under a new `/* Chat History section */` comment block.
- [x] 1.2 In `src/gui/AppController+LayoutAndTabs.inc`, create a new disclosure section `Chat History` between Mnemosyne and Encoding (or wherever fits the existing visual order).
- [x] 1.3 Inside the section, lay out groups in the order specified in `proposal.md`: Master → Retention → Legacy compat → Encryption at rest → Advanced (folded).
- [x] 1.4 Use `addCheckboxWithHelp` for booleans and `addRow`/`addRowWithButton` for fields — match the helpers used in the HOPE and Mnemosyne sections.

## 2. Master-toggle grey-out

- [x] 2.1 Declare `- (void)updateChatHistoryWidgetEnablement;` in `AppController.h`.
- [x] 2.2 Implement it in a new `src/gui/AppController+ChatHistoryActions.inc` (mirror the pattern of other `+Actions.inc` files). Helper iterates over every chat-history widget except the master checkbox and calls `setEnabled:` with the master checkbox state.
- [x] 2.3 Wire the master checkbox's action to a new `- (void)chatHistoryToggled:(id)sender` that calls the helper and triggers a save. Set `[_chatHistoryEnabledCheckbox setTarget:self]` and `[_chatHistoryEnabledCheckbox setAction:@selector(chatHistoryToggled:)]` at section creation time.
- [x] 2.4 Call the helper at the end of `loadSettings` in `LifecycleConfig.inc` so the initial state matches the loaded YAML.

## 3. Generate-key one-click

- [x] 3.1 Add `- (void)generateChatHistoryKey:(id)sender;` to `AppController.h` and implement in `+ChatHistoryActions.inc`.
- [x] 3.2 Default target path: `[_configDir stringByAppendingPathComponent:@"chat_history.key"]`.
- [x] 3.3 If a file already exists at the target path, present an `NSAlert` ("A key file already exists at this path. Generating a new key will make all existing encrypted messages unreadable.") with destructive-action confirmation and a "Choose a different filename" option that opens a save panel.
- [x] 3.4 Read 32 random bytes via `arc4random_buf(buf, 32)` (POSIX/BSD, no Security.framework dep needed; cryptographically secure on macOS). Stack copy zeroed after wrapping in NSData.
- [x] 3.5 Write to disk with `0600` permissions using `NSFileManager` + `setAttributes:ofItemAtPath:`. Set `NSFilePosixPermissions` to `@(0600)`.
- [x] 3.6 On success, populate `_chatHistoryKeyPathField` with the path and call `chatHistoryToggled:` (or whatever triggers save) so the new path lands in the next `writeConfigToDisk`.
- [x] 3.7 Wire the button at section creation with `makeButton(@"Generate Key…", self, @selector(generateChatHistoryKey:))`.

## 4. Load and save plumbing

- [x] 4.1 In `LifecycleConfig.inc` `loadSettings`, read each of the 9 `ChatHistory*` keys from the loaded dictionary and populate the corresponding widget. Use the same idiom as the existing `boolVal = [dict objectForKey:@"…"]; if (boolVal && _widget) [_widget setState:…]` block.
- [x] 4.2 Default ON behaviour for `ChatHistoryEnabled` follows the parent design (default OFF for the feature, but the GUI honours whatever the YAML says — no special-case "default ON" in the load path).
- [x] 4.3 In `writeConfigToDisk`, set each `ChatHistory*` key in the saved dictionary from the corresponding widget's value. Use `[NSNumber numberWithBool:…]` for booleans, `[NSNumber numberWithUnsignedInt:…]` for integer fields, plain `NSString` for the key path.
- [ ] 4.4 Round-trip test by hand: load a config that has chat-history fields, edit an unrelated setting, save, and verify the chat-history fields are still present in the YAML and unchanged. **Needs manual UX verification — code paths in place, but no automated round-trip test exists.**

## 5. Advanced (folded) sub-disclosure

- [x] 5.1 Verify `makeDisclosureHeader` works for in-section nesting (suspected yes — it's just an NSButton that toggles visibility of a content view). If it doesn't, fall back to read-only labels showing the current values and a hint to edit YAML directly (per design.md pitfall).
- [x] 5.2 Inside the folded view, lay out `Capacity` and `Refill per sec` integer fields. Default state: collapsed.
- [x] 5.3 Make sure the `updateChatHistoryWidgetEnablement` helper also disables these two fields when the master is OFF.

## 6. Version bump

- [x] 6.1 Update `src/main.c` line 326: `printf("lemoniscate 0.1.7\n");` → `0.1.8`.
- [x] 6.2 Update `src/hotline/hope.c` line 35: `#define HOPE_APP_STRING "Lemoniscate 0.1.7"` → `0.1.8`.
- [x] 6.3 Skip PPC branch (`ppc/man/lemoniscate.1`, `ppc/resources/Info.plist`) — out of scope per proposal.
- [x] 6.4 `make clean && make app` — verify both `lemoniscate --version` and the HOPE handshake banner show `0.1.8`.

## 7. Operator docs

- [x] 7.1 Add a short subsection to `docs/SERVER.md` titled "Rate limits: per-IP connection vs. per-connection chat" with the disjoint-counters explanation from `design.md` decision 4.

## 8. Build and deploy

- [x] 8.1 `make gui` — verify clean GUI build on macOS, no new warnings.
- [x] 8.2 `docker build -t lemoniscate-build .` — verify Linux server still builds.
- [x] 8.3 Deploy the new Linux binary to `hotline.semihosted.xyz` (server functionality is unchanged but version string updates).
- [ ] 8.4 Open the macOS GUI against an existing config dir, verify the new section appears, master toggle greys/un-greys children, generate-key writes a file, save round-trips all 9 keys. **Needs manual UX verification.**

## 9. Parent-change bookkeeping

- [x] 9.1 In `openspec/changes/chat-history/tasks.md`, mark task 2.5 done with a note: "Replaced by `chat-history-gui` change, landed in 0.1.8."
- [x] 9.2 In the same file, mark task 13.4 done with a note: "Already enabled on hotline.semihosted.xyz — log shows hourly prune timer firing since deploy. Awaits client smoke tests (13.5, 13.6)."
