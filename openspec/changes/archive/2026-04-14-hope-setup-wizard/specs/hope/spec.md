## MODIFIED Requirements

### Requirement: File transfer encryption enforcement

The file download and upload transaction handlers SHALL check the logged-in account's `require_encryption` flag before processing the transfer.

If `require_encryption` is set and the client does not have active HOPE transport encryption (as determined by `hl_client_is_encrypted()`), the handler SHALL reject the transaction with an error reply.

This check is in addition to the existing E2E prefix checks and applies to all files, not just those in prefixed folders.

#### Scenario: Download rejected for unencrypted client with require_encryption

- **WHEN** a client requests a file download, the client's account has `require_encryption = 1`, and `hl_client_is_encrypted()` returns 0
- **THEN** the handler SHALL return an error reply with message "This account requires an encrypted connection for file transfers."

#### Scenario: Upload rejected for unencrypted client with require_encryption

- **WHEN** a client requests a file upload, the client's account has `require_encryption = 1`, and `hl_client_is_encrypted()` returns 0
- **THEN** the handler SHALL return an error reply with message "This account requires an encrypted connection for file transfers."

#### Scenario: Encrypted client passes require_encryption check

- **WHEN** a client requests a file transfer, the client's account has `require_encryption = 1`, and `hl_client_is_encrypted()` returns 1
- **THEN** the transfer SHALL proceed normally (the require_encryption check passes)

#### Scenario: Account without require_encryption flag

- **WHEN** a client requests a file transfer and the client's account has `require_encryption = 0`
- **THEN** the require_encryption check SHALL be skipped (existing behavior preserved)
