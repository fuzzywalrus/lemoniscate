## 1. IBOutlet Declarations

- [x] 1.1 Add Mnemosyne IBOutlet ivars to AppController.h: `_mnemosyneEnableCheckbox`, `_mnemosyneURLField`, `_mnemosyneAPIKeyField`, `_mnemosyneIndexFilesCheckbox`, `_mnemosyneIndexNewsCheckbox`, `_mnemosyneIndexMsgboardCheckbox`, `_mnemosyneSavedURL` (NSString ivar for toggle state)
- [x] 1.2 Add Encoding IBOutlet ivar to AppController.h: `_encodingPopup` (NSPopUpButton)

## 2. GUI Layout

- [x] 2.1 Add Encoding popup to the General section in AppController+LayoutAndTabs.inc (after the existing fields, before section close) with "Mac Roman" and "UTF-8" options
- [x] 2.2 Add "Mnemosyne Search" disclosure section in AppController+LayoutAndTabs.inc after TLS Encryption: enable checkbox, URL field, API Key field, three index checkboxes (Files, News, Message Board), help button with tooltip
- [x] 2.3 Wire the enable checkbox action to toggle enabled/disabled state of URL, API Key, and index checkbox fields

## 3. Config Persistence (Plist Write)

- [x] 3.1 In `writeConfigToDisk` (AppController+LifecycleConfig.inc), read Mnemosyne fields from GUI controls and write to plist dict: `MnemosyneURL`, `MnemosyneAPIKey`, `MnemosyneIndexFiles`, `MnemosyneIndexNews`, `MnemosyneIndexMsgboard`
- [x] 3.2 Replace hardcoded `@"macintosh"` Encoding value with the selected popup value (`macintosh` or `utf-8`)

## 4. Config Persistence (Plist Read)

- [x] 4.1 In `loadConfigFromDisk` (AppController+LifecycleConfig.inc), read Mnemosyne plist keys and populate GUI controls. Set enable checkbox state based on whether URL is non-empty. Default index checkboxes to on if keys are absent.
- [x] 4.2 Read `Encoding` plist key and select matching popup item. Default to "Mac Roman" if absent.

## 5. Server Config Reader

- [x] 5.1 In `config_plist.c`, add `plist_get_string` calls for `MnemosyneURL` and `MnemosyneAPIKey`, and `plist_get_bool` calls for `MnemosyneIndexFiles`, `MnemosyneIndexNews`, `MnemosyneIndexMsgboard`

## 6. Verification

- [x] 6.1 Build the GUI app and verify the Mnemosyne section appears with all controls
- [ ] 6.2 Verify config round-trips: set values, restart app, confirm they persist (manual)
- [ ] 6.3 Verify the server process receives Mnemosyne config via log output (manual)
