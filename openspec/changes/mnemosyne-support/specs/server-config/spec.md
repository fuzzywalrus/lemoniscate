## ADDED Requirements

### Requirement: Mnemosyne configuration section in YAML
The server SHALL support a `mnemosyne` section in the YAML configuration file with the following keys: `url` (string, required to enable sync), `api_key` (string, required — operator API key from Mnemosyne registration), `server_id` (string, required — operator-chosen slug matching `^[a-z0-9_-]{3,32}$`), `sync_interval` (integer, seconds, default 900), and `content_types` (list of strings, default `[msgboard, news, files]`).

#### Scenario: Full Mnemosyne configuration
- **WHEN** the config file contains a `mnemosyne` section with `url`, `api_key`, `server_id`, `sync_interval`, and `content_types`
- **THEN** the server parses all Mnemosyne settings and enables sync with the specified parameters

#### Scenario: Minimal Mnemosyne configuration
- **WHEN** the config file contains a `mnemosyne` section with `url`, `api_key`, and `server_id`
- **THEN** the server enables sync with default values: 900-second interval, all content types

#### Scenario: No Mnemosyne section
- **WHEN** the config file does not contain a `mnemosyne` section
- **THEN** Mnemosyne sync is disabled and no sync-related resources are initialized

#### Scenario: Missing required Mnemosyne fields
- **WHEN** the `mnemosyne` section is present but `api_key` or `server_id` is missing
- **THEN** the server logs a warning identifying the missing field and disables Mnemosyne sync

#### Scenario: Invalid content type in configuration
- **WHEN** the `content_types` list contains an unrecognized value
- **THEN** the server logs a warning for the unrecognized value and ignores it

#### Scenario: Invalid server_id format
- **WHEN** the `server_id` does not match `^[a-z0-9_-]{3,32}$`
- **THEN** the server logs an error and disables Mnemosyne sync

### Requirement: SIGHUP reloads Mnemosyne configuration
The server SHALL reload Mnemosyne configuration on SIGHUP, allowing operators to change the sync URL, API key, server ID, interval, and content types without restarting the server.

#### Scenario: SIGHUP updates Mnemosyne URL
- **WHEN** the server receives SIGHUP and the config file has a changed Mnemosyne URL
- **THEN** the server re-probes the new Mnemosyne instance and updates the sync target

#### Scenario: SIGHUP disables Mnemosyne
- **WHEN** the server receives SIGHUP and the Mnemosyne section has been removed from the config
- **THEN** the server stops the sync timer and disables Mnemosyne sync
