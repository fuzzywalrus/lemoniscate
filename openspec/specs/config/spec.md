## MODIFIED Requirements

### Requirement: Setup wizard step sequence

The setup wizard SHALL consist of 5 steps:

1. Identity & Network
2. Paths & Registration
3. Limits & File Behavior
4. **Encryption** (new)
5. Summary

The progress bar maximum SHALL be 5.0. The step label SHALL show "Step N of 5: Title".

#### Scenario: Wizard step navigation

- **WHEN** the operator navigates through the wizard
- **THEN** the steps SHALL appear in order: Identity & Network, Paths & Registration, Limits & File Behavior, Encryption, Summary

#### Scenario: Summary is the final step

- **WHEN** the operator reaches step 5 (Summary)
- **THEN** the Finish and Finish and Start buttons SHALL be visible, and the Next button SHALL be hidden

### Requirement: Encryption wizard screen content

The Encryption wizard step SHALL present encryption settings in plain, non-technical English.

The screen SHALL include:
- A heading or intro explaining that Lemoniscate can encrypt all communication
- An "Enable encryption (recommended)" checkbox, on by default, mapping to `EnableHOPE`
- An "E2E Prefix" text field defaulting to `[E2E]`, mapping to `HOPERequiredPrefix`
- A "Require encryption for admin account file transfers" checkbox, off by default
- A note stating that TLS for Hotline 1.9+ clients can be configured in Settings after setup

The screen SHALL NOT mention ChaCha20-Poly1305, HMAC-SHA256, AEAD, or other cryptographic implementation details.

#### Scenario: Default state of encryption screen

- **WHEN** the operator reaches the Encryption step for the first time
- **THEN** "Enable encryption" SHALL be checked, "E2E Prefix" SHALL show "[E2E]", "Require encryption for admin" SHALL be unchecked

#### Scenario: Wizard applies encryption settings

- **WHEN** the operator finishes the wizard with encryption enabled
- **THEN** the `EnableHOPE` config value SHALL be set to true and the `HOPERequiredPrefix` SHALL be set to the entered prefix

#### Scenario: Wizard applies admin encryption requirement

- **WHEN** the operator finishes the wizard with "Require encryption for admin" checked
- **THEN** the admin account's `require_encryption` flag SHALL be set to 1

#### Scenario: Summary includes encryption choices

- **WHEN** the operator reaches the Summary step
- **THEN** the summary SHALL include the encryption status, E2E prefix, and admin encryption requirement
