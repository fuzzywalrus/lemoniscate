## ADDED Requirements

### Requirement: GUI Mnemosyne controls
The GUI SHALL provide a disclosure section for configuring Mnemosyne search integration, including: enable/disable checkbox, URL field, API key field, and three index toggles (Files, News, Message Board).

#### Scenario: Enable Mnemosyne from GUI
- **WHEN** the user checks the Mnemosyne enable checkbox
- **THEN** the GUI SHALL enable the URL and API key fields, and write `MnemosyneURL`, `MnemosyneAPIKey`, `MnemosyneIndexFiles`, `MnemosyneIndexNews`, `MnemosyneIndexMsgboard` to the plist config

#### Scenario: Disable Mnemosyne from GUI
- **WHEN** the user unchecks the Mnemosyne enable checkbox
- **THEN** the GUI SHALL clear the URL field (matching the server's "URL empty = disabled" contract) and disable the configuration fields

#### Scenario: Display Mnemosyne connection status
- **WHEN** Mnemosyne sync is enabled and the GUI is running
- **THEN** the GUI SHALL display the current connection status (connected, disconnected, error) in the server status area

### Requirement: GUI encoding settings
The GUI SHALL provide a popup selector for text encoding behavior (Mac Roman / UTF-8). The default SHALL be Mac Roman to preserve compatibility with classic Hotline clients.

#### Scenario: Select encoding preference
- **WHEN** the user changes the encoding popup
- **THEN** the GUI SHALL write the `Encoding` key to the plist config

#### Scenario: Default encoding
- **WHEN** no encoding preference has been set
- **THEN** the GUI SHALL default to Mac Roman

### Requirement: GUI layout and tab overhaul
The GUI SHALL use an expanded tab-based layout for organizing server controls, logs, and settings. All layout code MUST use Tiger 10.4-compatible Cocoa APIs — no NSStackView, no auto-layout, no blocks, no GCD. Frame-based manual layout only.

#### Scenario: Tab navigation
- **WHEN** the user clicks a tab in the GUI
- **THEN** the GUI SHALL switch the visible content panel to the selected tab's content using manual frame-based layout

#### Scenario: Vespernet integration
- **WHEN** the GUI starts with Vespernet features enabled
- **THEN** the GUI SHALL display Vespernet-specific controls in the appropriate tab

### Requirement: GUI TLS certificate generation
The GUI SHALL allow generating TLS certificates directly from the interface without requiring command-line tools.

#### Scenario: Generate self-signed certificate
- **WHEN** the user clicks "Generate Certificate" in the TLS settings
- **THEN** the GUI SHALL generate a self-signed TLS certificate and private key, save them to the configured path, and update the server configuration
