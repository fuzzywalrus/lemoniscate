## Context

Lemoniscate's macOS GUI includes a setup wizard (`showSetupWizard:` in `AppController+GeneralActions.inc`) that runs on first launch or when the operator clicks "Setup Wizard" in the menu. It's a 4-step wizard: Identity & Network, Paths & Registration, Limits & File Behavior, Summary. Each step is built dynamically in `rebuildWizardStepUI` as a switch on `_wizardStepIndex`.

The account system uses `hl_account_t` (defined in `client_conn.h`) with login, name, password, access bitmap, and optional per-account file root. Accounts are persisted in YAML via `yaml_account_manager.c`. The Accounts tab in the GUI lets operators create/edit/delete accounts with a detail panel showing access permissions.

## Goals / Non-Goals

**Goals:**
- Add an "Encryption" wizard step that explains HOPE in plain, non-technical English
- Recommend enabling HOPE by default during setup
- Expose the E2E prefix setting with a clear explanation
- Add per-account `require_encryption` flag for enforcing encrypted file transfers
- Surface the per-account flag in both the wizard (for the admin account) and the Accounts tab
- Mention TLS as a separate, post-setup option for 1.9+ clients

**Non-Goals:**
- TLS configuration in the wizard (explicitly out of scope — mentioned as a post-setup option)
- Changing any HOPE protocol behavior (this is purely UI/UX + per-account enforcement)
- Folder transfers (not yet AEAD-enabled)

## Decisions

### Decision 1: Encryption screen as wizard step 3 (before Summary)

Insert the Encryption step between "Limits & File Behavior" (current step 2) and "Summary" (current step 3). The new order:

0. Identity & Network
1. Paths & Registration
2. Limits & File Behavior
3. **Encryption** (new)
4. Summary

The progress bar max changes from 4.0 to 5.0. The step boundary checks (`_wizardStepIndex >= 3`) shift to `>= 4`.

**Rationale:** Encryption comes after the basic server config is done but before the summary, so the operator can see their encryption choices in the review.

### Decision 2: Plain English copy for the Encryption screen

The screen text avoids jargon. No mention of "ChaCha20-Poly1305", "HMAC-SHA256", "AEAD", or "key derivation". Instead:

> **End-to-End Encryption**
>
> Lemoniscate can encrypt all communication between clients and this server, including chat messages, file listings, and file transfers.
>
> When encryption is enabled, modern Hotline clients like Navigator will automatically negotiate a secure connection. Older clients that don't support encryption can still connect — they just won't be able to see content in encrypted-only areas.
>
> You can designate folders as encrypted-only by starting their name with a prefix (default: **[E2E]**). Only clients with an encrypted connection will see these folders and their contents.

Controls:
- **[x] Enable encryption (recommended)** — maps to `EnableHOPE` config
- **E2E Prefix:** `[E2E]` — maps to `HOPERequiredPrefix`
- **[ ] Require encryption for admin account file transfers** — sets `require_encryption` on the initial admin account
- Gray note at bottom: *"TLS encryption for Hotline 1.9+ clients can be configured in Settings at any time."*

**Rationale:** Server operators aren't necessarily crypto-literate. The wizard should make them feel confident enabling encryption without needing to understand the underlying protocol.

### Decision 3: Per-account `require_encryption` flag

Add `int require_encryption` to `hl_account_t`. When set to 1:
- `handle_download_file` checks `cc->account->require_encryption` and calls `hl_client_is_encrypted(cc)`. If not encrypted, returns an error: "This account requires an encrypted connection for file transfers."
- `handle_upload_file` does the same check.
- This is independent of the E2E prefix system — it applies to ALL files for that account, regardless of folder name.

**Storage:** The YAML account manager reads/writes `require_encryption: true/false` in the account YAML file. Missing key defaults to false.

**GUI:** The Accounts tab detail panel gains a checkbox: "Require Encryption for File Transfers" below the existing access permissions.

### Decision 4: Wizard applies encryption settings to config

`applyWizardValuesToSettings` already copies wizard field values to the settings panel controls. The Encryption step adds:
- Copy `_wizardHopeCheckbox` state to `_hopeCheckbox`
- Copy `_wizardE2EPrefixField` value to `_hopePrefixField`
- If `_wizardRequireEncryptionCheckbox` is on, set `require_encryption = 1` on the admin account after it's created

The admin account creation already happens in the wizard finish flow (or the first-run auto-account setup). The encryption flag is applied after the account is written.

### Decision 5: Summary includes encryption status

The Summary step (now step 4) includes the encryption choices:
```
Encryption: Enabled
E2E Prefix: [E2E]
Admin Requires Encryption: Yes
```

## Risks / Trade-offs

**[Risk] Operators enable encryption but use non-HOPE clients** -> The wizard text explicitly says "older clients can still connect" — they just can't see E2E content. This is the correct behavior and is already implemented.

**[Trade-off] Per-account flag is coarse-grained** -> It applies to all files, not per-folder. This is intentional — if an operator wants folder-level gating, they use the E2E prefix. The per-account flag is for operators who want to ensure a specific user always uses encryption.

**[Trade-off] No TLS in wizard** -> TLS requires certificate files and is more complex to set up. Including it in the wizard would make the Encryption step too long. The note pointing to Settings is sufficient.
