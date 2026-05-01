# Spec — Chat History GUI

## ADDED Requirements

### Requirement: Chat history settings section in the admin GUI
The admin GUI SHALL expose every persisted `ChatHistory*` configuration key through native widgets so that GUI saves preserve those keys instead of stripping them from `config.yaml`.

#### Scenario: All chat-history keys round-trip through a GUI save
- **WHEN** the operator loads a config that contains all 9 `ChatHistory*` keys
- **AND** the operator changes an unrelated setting (e.g. server name)
- **AND** the operator saves
- **THEN** the resulting `config.yaml` still contains all 9 `ChatHistory*` keys
- **AND** the values of those keys are unchanged

#### Scenario: An operator who has never used chat history sees the section
- **WHEN** the operator loads a config that contains no `ChatHistory*` keys
- **THEN** the GUI shows the Chat History section with the master toggle OFF
- **AND** the section's other widgets are visible but disabled

### Requirement: Master-toggle gating
The Chat History section's `Enable chat history` checkbox SHALL gate the enabled state of every other widget in the section.

#### Scenario: Master toggle off disables children
- **WHEN** the operator unchecks `Enable chat history`
- **THEN** every other widget in the Chat History section becomes disabled
- **AND** the widgets remain visible (not hidden)

#### Scenario: Master toggle on enables children
- **WHEN** the operator checks `Enable chat history`
- **THEN** every other widget in the Chat History section becomes enabled

#### Scenario: Initial widget state matches loaded config
- **WHEN** a config with `ChatHistoryEnabled: true` is loaded
- **THEN** every chat-history widget is enabled on first paint
- **WHEN** a config with `ChatHistoryEnabled: false` (or absent) is loaded
- **THEN** every chat-history widget except the master checkbox is disabled on first paint

### Requirement: One-click encryption-key generation
The Chat History section SHALL provide a `Generate Key…` button that writes a 32-byte random key to disk with `0600` permissions and populates the key path field.

#### Scenario: Generating a key into an empty path
- **WHEN** the operator clicks `Generate Key…`
- **AND** no file exists at the default target path `<configdir>/chat_history.key`
- **THEN** a 32-byte file is written at the default path with permissions `0600`
- **AND** the key path field is populated with the path
- **AND** the next config save persists the new path

#### Scenario: Generating a key into a path that already has one
- **WHEN** the operator clicks `Generate Key…`
- **AND** a file exists at the target path
- **THEN** the GUI presents a confirmation alert warning that overwriting will make all existing encrypted messages unreadable
- **AND** the alert offers the operator a `Choose different filename…` action that opens a save panel

### Requirement: Rate-limit knobs are folded behind an Advanced disclosure
The two rate-limit fields (`ChatHistoryRateCapacity`, `ChatHistoryRateRefillPerSec`) SHALL live behind an `Advanced` disclosure within the Chat History section, folded by default.

#### Scenario: Advanced fields are hidden by default
- **WHEN** the GUI is opened
- **THEN** the rate-limit fields are not visible
- **AND** an `Advanced` disclosure header is visible at the bottom of the Chat History section

#### Scenario: Expanding Advanced reveals the rate-limit fields
- **WHEN** the operator clicks the `Advanced` disclosure header
- **THEN** the two rate-limit fields become visible
- **AND** their enabled state honours the master toggle (still disabled if chat history is off)

### Requirement: Version bump to 0.1.8
The release that ships this GUI SHALL identify itself as `0.1.8` in both the CLI version output and the HOPE protocol app-string.

#### Scenario: CLI reports new version
- **WHEN** `lemoniscate --version` is invoked
- **THEN** the output is `lemoniscate 0.1.8`

#### Scenario: HOPE handshake reports new version
- **WHEN** a HOPE handshake completes and the server-info string is exchanged
- **THEN** the app string is `Lemoniscate 0.1.8`
