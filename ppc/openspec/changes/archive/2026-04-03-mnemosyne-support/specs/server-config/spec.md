## ADDED Requirements

### Requirement: Mnemosyne configuration section in YAML
The server SHALL support a `mnemosyne` section in the YAML configuration file with the following keys: `url` (string, required to enable sync), `api_key` (string, required — `msv_`-prefixed server API key), `index_files` (boolean, default true), `index_news` (boolean, default true), `index_msgboard` (boolean, default true).

#### Scenario: Full Mnemosyne configuration
- **WHEN** the config file contains a `mnemosyne` section with `url`, `api_key`, and content type toggles
- **THEN** the server parses all Mnemosyne settings and enables sync with the specified parameters

#### Scenario: Minimal Mnemosyne configuration
- **WHEN** the config file contains a `mnemosyne` section with only `url` and `api_key`
- **THEN** the server enables sync with all content types indexed by default

#### Scenario: No Mnemosyne section
- **WHEN** the config file does not contain a `mnemosyne` section
- **THEN** Mnemosyne sync is disabled and no sync-related timers are registered

#### Scenario: Missing API key
- **WHEN** the `mnemosyne` section has `url` but no `api_key`
- **THEN** the server logs a warning and disables Mnemosyne sync

### Requirement: SIGHUP reloads Mnemosyne configuration
The server SHALL reload Mnemosyne configuration on SIGHUP, allowing operators to change the sync URL, API key, and content type toggles without restarting.

#### Scenario: SIGHUP updates Mnemosyne URL
- **WHEN** the server receives SIGHUP and the config file has a changed Mnemosyne URL
- **THEN** the server updates the sync target and triggers a fresh full sync

#### Scenario: SIGHUP disables Mnemosyne
- **WHEN** the server receives SIGHUP and the Mnemosyne section has been removed
- **THEN** the server stops all sync timers and sends a deregister POST
