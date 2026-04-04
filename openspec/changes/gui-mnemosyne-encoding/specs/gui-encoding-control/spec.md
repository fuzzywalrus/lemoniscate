## ADDED Requirements

### Requirement: Encoding popup in General section
The GUI SHALL include a "Text Encoding" popup button in the General section with two options: "Mac Roman" and "UTF-8".

#### Scenario: Popup shows current encoding
- **WHEN** the General tab loads with an existing config
- **THEN** the popup displays the encoding matching the saved config value

#### Scenario: Default is Mac Roman
- **WHEN** no prior config exists
- **THEN** the popup defaults to "Mac Roman"

### Requirement: Encoding selection maps to config values
Selecting "Mac Roman" SHALL write `macintosh` to the config. Selecting "UTF-8" SHALL write `utf-8`.

#### Scenario: User selects UTF-8
- **WHEN** the user selects "UTF-8" from the popup
- **THEN** the plist saves `Encoding` as `utf-8`

#### Scenario: User selects Mac Roman
- **WHEN** the user selects "Mac Roman" from the popup
- **THEN** the plist saves `Encoding` as `macintosh`

### Requirement: Encoding persists in plist
The encoding selection SHALL be saved to and loaded from the plist config file, replacing the hardcoded `"macintosh"` value.

#### Scenario: Round-trip persistence
- **WHEN** the user selects "UTF-8", saves, and restarts the app
- **THEN** the popup shows "UTF-8" on relaunch
