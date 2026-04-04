## ADDED Requirements

### Requirement: Mnemosyne disclosure section exists
The GUI SHALL include a "Mnemosyne Search" disclosure section in the General tab, placed after the TLS Encryption section.

#### Scenario: Section visible on launch
- **WHEN** the user opens the General tab
- **THEN** a "Mnemosyne Search" collapsible section is visible

### Requirement: Enable checkbox toggles sync
The section SHALL contain an "Enable Mnemosyne Sync" checkbox. When unchecked, the URL and API Key fields SHALL be disabled. When checked, they SHALL be enabled.

#### Scenario: Unchecking disables fields
- **WHEN** the user unchecks "Enable Mnemosyne Sync"
- **THEN** the URL and API Key fields are disabled and the URL value is cleared from the saved config

#### Scenario: Checking enables fields with default URL
- **WHEN** the user checks "Enable Mnemosyne Sync" and the URL field is empty
- **THEN** the URL field is populated with `http://tracker.vespernet.net:8980` and both fields are enabled

### Requirement: URL text field
The section SHALL contain a "URL" text field for the Mnemosyne instance URL.

#### Scenario: URL is editable
- **WHEN** Mnemosyne is enabled
- **THEN** the user can type or paste a URL into the field

### Requirement: API Key text field
The section SHALL contain an "API Key" text field displayed as visible plain text.

#### Scenario: API key is editable and visible
- **WHEN** Mnemosyne is enabled
- **THEN** the user can type or paste an API key and the text is visible (not masked)

### Requirement: Index checkboxes
The section SHALL contain three checkboxes: "Files", "News", and "Message Board", all defaulting to on.

#### Scenario: All index options default to checked
- **WHEN** no prior config exists
- **THEN** all three index checkboxes are checked

#### Scenario: User disables file indexing
- **WHEN** the user unchecks "Files"
- **THEN** `MnemosyneIndexFiles` is saved as false in the plist

### Requirement: Mnemosyne config persists in plist
All Mnemosyne fields SHALL be saved to and loaded from the plist config file.

#### Scenario: Round-trip persistence
- **WHEN** the user configures Mnemosyne and restarts the app
- **THEN** all Mnemosyne fields (URL, API key, index toggles) are restored to their saved values

### Requirement: Mnemosyne config read by server
The plist reader (`config_plist.c`) SHALL populate the `hl_config_t` Mnemosyne fields from the plist.

#### Scenario: Server receives Mnemosyne config
- **WHEN** the server process starts with a plist containing Mnemosyne config
- **THEN** `cfg->mnemosyne_url`, `cfg->mnemosyne_api_key`, and index flags are populated
