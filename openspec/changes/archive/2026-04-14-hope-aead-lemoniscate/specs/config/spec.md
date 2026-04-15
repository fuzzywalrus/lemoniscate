## MODIFIED Requirements

### Requirement: HOPE configuration fields

The server configuration SHALL support the following HOPE-related fields:

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `enable_hope` | bool | false | Master HOPE switch |
| `hope_legacy_mode` | bool | false | Allow weak algorithms (INVERSE, bare SHA1/MD5) |
| `hope_required_prefix` | string | `"[E2E]"` | Name prefix for E2E-only content |
| `e2e_require_tls` | bool | false | Require TLS for E2E file access |
| `hope_cipher_policy` | string | `"prefer-aead"` | Cipher negotiation policy |
| `e2e_require_aead` | bool | false | Require AEAD for E2E content access |

The `hope_cipher_policy` field SHALL accept the following string values:
- `"prefer-aead"`: select CHACHA20-POLY1305 if client supports it, fall back to RC4
- `"require-aead"`: only accept CHACHA20-POLY1305, reject RC4-only HOPE clients
- `"rc4-only"`: only offer RC4 (existing behavior)

Unknown values SHALL be treated as `"prefer-aead"` with a warning log.

#### Scenario: YAML config with cipher policy

- **WHEN** the YAML config file contains `hope_cipher_policy: require-aead`
- **THEN** the server SHALL parse and apply the `require-aead` cipher policy

#### Scenario: Plist config with cipher policy

- **WHEN** the plist config contains key `HOPECipherPolicy` with string value `"prefer-aead"`
- **THEN** the server SHALL parse and apply the `prefer-aead` cipher policy

#### Scenario: YAML config with e2e_require_aead

- **WHEN** the YAML config file contains `e2e_require_aead: true`
- **THEN** the server SHALL enable AEAD-specific E2E gating

#### Scenario: Plist config with E2ERequireAEAD

- **WHEN** the plist config contains key `E2ERequireAEAD` with boolean value true
- **THEN** the server SHALL enable AEAD-specific E2E gating

#### Scenario: Default cipher policy

- **WHEN** neither YAML nor plist config specifies `hope_cipher_policy`
- **THEN** the server SHALL default to `prefer-aead`

#### Scenario: Unknown cipher policy value

- **WHEN** the config contains `hope_cipher_policy: foo`
- **THEN** the server SHALL log a warning and default to `prefer-aead`
