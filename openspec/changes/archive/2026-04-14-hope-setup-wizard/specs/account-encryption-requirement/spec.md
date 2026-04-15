## ADDED Requirements

### Requirement: Per-account encryption requirement for file transfers

The system SHALL support a `require_encryption` flag on each user account. When set, the account's file download and upload transactions SHALL be rejected if the client does not have active HOPE transport encryption.

The flag SHALL be independent of the global E2E prefix gating — it applies to all files regardless of folder name.

The flag SHALL default to off (0) for all accounts.

#### Scenario: Encrypted client with require_encryption enabled

- **WHEN** a client logged in with an account that has `require_encryption = 1` requests a file download, and the client has active HOPE encryption
- **THEN** the download SHALL proceed normally

#### Scenario: Unencrypted client with require_encryption enabled

- **WHEN** a client logged in with an account that has `require_encryption = 1` requests a file download, and the client does NOT have active HOPE encryption
- **THEN** the server SHALL reject the download with an error message indicating that encryption is required

#### Scenario: Upload rejected for unencrypted client

- **WHEN** a client logged in with an account that has `require_encryption = 1` requests a file upload, and the client does NOT have active HOPE encryption
- **THEN** the server SHALL reject the upload with an error message indicating that encryption is required

#### Scenario: require_encryption disabled (default)

- **WHEN** an account has `require_encryption = 0` (the default)
- **THEN** file transfers SHALL proceed regardless of encryption status (existing behavior)

### Requirement: Account encryption flag persistence

The `require_encryption` flag SHALL be persisted in the account YAML file as a boolean field. Accounts without the field SHALL default to `require_encryption = 0`.

#### Scenario: Save account with require_encryption

- **WHEN** an account is saved with `require_encryption = 1`
- **THEN** the YAML file SHALL contain `require_encryption: true`

#### Scenario: Load account without require_encryption field

- **WHEN** an account YAML file does not contain `require_encryption`
- **THEN** the loaded account SHALL have `require_encryption = 0`

### Requirement: Account encryption flag in GUI

The Accounts tab detail panel SHALL include a "Require Encryption for File Transfers" checkbox that sets the `require_encryption` flag on the account.

#### Scenario: Checkbox reflects account state

- **WHEN** the operator selects an account in the Accounts tab
- **THEN** the "Require Encryption for File Transfers" checkbox SHALL reflect the account's `require_encryption` value

#### Scenario: Checkbox saves to account

- **WHEN** the operator toggles the "Require Encryption for File Transfers" checkbox and saves
- **THEN** the account's `require_encryption` flag SHALL be updated
