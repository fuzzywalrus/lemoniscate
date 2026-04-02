## ADDED Requirements

### Requirement: YAML configuration file loading
The server SHALL load its configuration from a YAML file (default `config.yaml`) at startup. The configuration includes server name, description, port numbers, file root path, tracker list, encoding, and banner path.

#### Scenario: Valid config file
- **WHEN** the server starts with a valid YAML configuration file
- **THEN** it parses all settings and applies them to the server instance

#### Scenario: Missing config file
- **WHEN** the server starts and the specified config file does not exist
- **THEN** the server exits with an error message indicating the missing file

#### Scenario: Invalid YAML syntax
- **WHEN** the config file contains invalid YAML
- **THEN** the server exits with a parse error message

### Requirement: Plist configuration support
The server SHALL also support loading configuration from macOS plist files as an alternative to YAML, enabling integration with macOS system preferences and the GUI admin interface.

#### Scenario: Plist config loading
- **WHEN** the server is given a plist configuration file
- **THEN** it parses the plist and applies settings equivalently to YAML configuration

### Requirement: --init scaffolds a default configuration directory
The server SHALL support an `--init` flag that creates a default configuration directory containing: `config.yaml`, `Agreement.txt`, `MessageBoard.txt`, `Banlist.yaml`, a `Files/` directory, and a `Users/` directory with default admin and guest accounts.

#### Scenario: Initialize fresh config
- **WHEN** the server is run with `--init` and the target directory does not exist
- **THEN** it creates the full directory structure with default files and exits

#### Scenario: Init on existing directory
- **WHEN** the server is run with `--init` and the target directory already exists
- **THEN** it does not overwrite existing files

### Requirement: CLI flags override configuration
The server SHALL accept command-line flags that override values from the config file. Flags include port, config path, interface binding, and log level.

#### Scenario: Port override via CLI
- **WHEN** the server is started with a `--port` flag
- **THEN** the server listens on the specified port instead of the config file value

### Requirement: SIGHUP reloads configuration
The server SHALL reload its configuration from disk when it receives a SIGHUP signal, without requiring a restart. Reloaded settings include server name, description, agreement, banner, and tracker list.

#### Scenario: SIGHUP triggers reload
- **WHEN** the server process receives SIGHUP
- **THEN** it re-reads the config file and applies updated settings without disconnecting existing clients

### Requirement: Agreement file serving
The server SHALL serve a configurable agreement text file to clients upon login. The agreement file path is specified in the server configuration.

#### Scenario: Agreement file exists
- **WHEN** the server starts with an agreement file path configured and the file exists
- **THEN** the agreement content is loaded and sent to clients after login

#### Scenario: Agreement file missing
- **WHEN** the configured agreement file does not exist
- **THEN** the server starts without an agreement (no agreement sent to clients)

### Requirement: Ban list persistence
The server SHALL persist the ban list to a YAML file (`Banlist.yaml`). Bans added at runtime are saved to disk and loaded on startup.

#### Scenario: Ban list loaded on startup
- **WHEN** the server starts with a Banlist.yaml file present
- **THEN** it loads all ban entries and enforces them immediately

#### Scenario: Runtime ban persisted
- **WHEN** a privileged user adds a ban during server operation
- **THEN** the ban is written to Banlist.yaml and takes effect immediately

### Requirement: Graceful shutdown on SIGINT/SIGTERM
The server SHALL shut down gracefully when receiving SIGINT or SIGTERM, closing all client connections, deregistering from trackers and Bonjour, and flushing any pending data.

#### Scenario: Graceful shutdown
- **WHEN** the server receives SIGINT or SIGTERM
- **THEN** it stops accepting new connections, disconnects all clients, deregisters services, and exits cleanly
