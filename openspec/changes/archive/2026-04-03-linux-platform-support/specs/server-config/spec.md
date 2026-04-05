## ADDED Requirements

### Requirement: Plist config is macOS-only
Plist configuration loading SHALL only be available on macOS. On Linux, the server SHALL use YAML configuration exclusively. No error or warning SHALL be emitted about plist unavailability on Linux.

#### Scenario: Linux config loading
- **WHEN** the server starts on Linux
- **THEN** it loads configuration from YAML only and does not attempt plist loading

#### Scenario: macOS config loading unchanged
- **WHEN** the server starts on macOS
- **THEN** it attempts plist loading first, then falls back to YAML, as before
