## Why

Lemoniscate now has full HOPE AEAD encryption (ChaCha20-Poly1305) for control connections and file transfers, but operators have no way to discover or enable it during initial setup. The HOPE settings are buried in the Security section of the settings panel. Most operators don't know what HOPE is or why they should turn it on. The setup wizard should explain encryption in plain English, recommend enabling it, and offer per-account encryption requirements so operators can enforce encrypted file transfers from the start.

## What Changes

- Add a new "Encryption" screen (step 3) to the 4-step setup wizard, making it a 5-step wizard
- The Encryption screen explains HOPE in plain English: what it does, why it matters, and how encrypted-only content areas work
- Enable HOPE by default in the wizard (checkbox on)
- Let operators set the E2E prefix for encrypted-only content areas
- Let operators optionally require encryption for the admin account's file transfers
- Add a `require_encryption` flag to `hl_account_t` (per-account, off by default)
- When set, file download and upload handlers reject transfers from unencrypted clients
- Include a note that TLS is for Hotline 1.9+ clients and can be configured in Settings after setup (not part of the wizard)
- Add the `require_encryption` checkbox to the Accounts tab in the settings GUI

## Capabilities

### New Capabilities
- `account-encryption-requirement`: Per-account flag that requires HOPE encryption for file transfers (downloads and uploads). Independent of the global E2E prefix gating.

### Modified Capabilities
- `config`: Wizard gains a 5th step for encryption settings
- `hope`: Download and upload transaction handlers check the account's `require_encryption` flag in addition to the existing E2E prefix checks

## Impact

- **Wizard flow**: Changes from 4 steps to 5 steps. Progress bar max changes from 4.0 to 5.0. Step indices shift: old step 3 (Summary) becomes step 4.
- **Account storage**: `hl_account_t` gains `require_encryption` field. YAML account files gain `require_encryption` key. Existing accounts default to 0.
- **Transaction handlers**: `handle_download_file`, `handle_upload_file` check the per-account flag and return an error if the client is not encrypted.
- **GUI**: Accounts tab gains a "Require Encryption for File Transfers" checkbox. Wizard gains new ivars for encryption step controls.
