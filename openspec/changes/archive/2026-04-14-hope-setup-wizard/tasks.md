## 1. Per-Account Encryption Flag

- [x] 1.1 Add `int require_encryption` field to `hl_account_t` in `client_conn.h`
- [x] 1.2 Update `yaml_account_manager.c` to read `require_encryption` from YAML (default false)
- [x] 1.3 Update `yaml_account_manager.c` to write `require_encryption` to YAML
- [x] 1.4 Add encryption check to `handle_download_file` in `transaction_handlers_clean.c`: if `cc->account->require_encryption && !hl_client_is_encrypted(cc)`, return error reply
- [x] 1.5 Add encryption check to `handle_upload_file` in `transaction_handlers_clean.c`: same check as download

## 2. Accounts Tab GUI

- [x] 2.1 Add `_acctRequireEncryptionCheckbox` ivar to AppController.h
- [x] 2.2 Create the checkbox in the account detail panel in `LayoutAndTabs.inc`, labeled "Require Encryption for File Transfers"
- [x] 2.3 Load the checkbox state when an account is selected in the Accounts tab
- [x] 2.4 Save the checkbox state when the account is saved/updated

## 3. Wizard Expansion (4 steps to 5)

- [x] 3.1 Change progress bar max from 4.0 to 5.0 in `showSetupWizard:`
- [x] 3.2 Update step boundary checks: Next button hidden at `_wizardStepIndex >= 4`, Finish buttons shown at `_wizardStepIndex >= 4`
- [x] 3.3 Update step title switch in `rebuildWizardStepUI`: add case 3 = "Encryption", shift Summary to case 4
- [x] 3.4 Update step label format to "Step N of 5"

## 4. Wizard Encryption Screen (Step 3)

- [x] 4.1 Add wizard encryption ivars: `_wizardHopeCheckbox`, `_wizardE2EPrefixField`, `_wizardRequireEncryptionCheckbox`
- [x] 4.2 Allocate the controls in `showSetupWizard:` alongside existing wizard controls
- [x] 4.3 Build the Encryption step UI in `rebuildWizardStepUI` case 3:
  - Intro text explaining encryption in plain English
  - "Enable encryption (recommended)" checkbox (on by default)
  - "E2E Prefix:" label + text field (default "[E2E]")
  - Explanation of what the prefix does
  - "Require encryption for admin account file transfers" checkbox (off by default)
  - Gray note: "TLS encryption for Hotline 1.9+ clients can be configured in Settings at any time."
- [x] 4.4 Shift existing Summary step code from `case 3:` (default) to `case 4:` (or default)

## 5. Wizard Apply & Summary

- [x] 5.1 Update Summary step to include encryption settings (Encryption: Enabled/Disabled, E2E Prefix, Admin Requires Encryption)
- [x] 5.2 Update `applyWizardValuesToSettings` to copy wizard HOPE checkbox to `_hopeCheckbox`, wizard E2E prefix to `_hopePrefixField`
- [x] 5.3 Update `applyWizardValuesToSettings` to set `require_encryption` on the admin account if the wizard checkbox is checked
- [x] 5.4 Update `validateWizardStep:` if needed for the new step (no validation required — all fields have defaults)
