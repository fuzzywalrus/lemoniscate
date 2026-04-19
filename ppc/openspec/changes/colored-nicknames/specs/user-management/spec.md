## MODIFIED Requirements

### Requirement: Account YAML schema

Account YAML files SHALL support an optional `Color` key in addition to existing fields. When present, `Color` SHALL be a string in the form `"#RRGGBB"` (case-insensitive hex). When absent or empty, the account has no admin-assigned color and the color cascade falls through to its next tier.

Invalid `Color` values (wrong length, non-hex characters) SHALL be logged at `warn` level and treated as absent.

#### Scenario: Account with Color key
- **WHEN** an account YAML file contains `Color: "#FFD700"`
- **THEN** the loaded account has `nick_color = 0x00FFD700`
- **AND** the color cascade uses this value as the highest-priority tier

#### Scenario: Account without Color key
- **WHEN** an account YAML file does not contain a `Color` key
- **THEN** the loaded account has `nick_color = 0`
- **AND** the color cascade skips tier 1 for this account

#### Scenario: Round-trip preserves Color
- **WHEN** an account with `Color: "#FFD700"` is parsed and re-serialized without edits
- **THEN** the output YAML contains `Color: "#FFD700"`

## ADDED Requirements

### Requirement: User notification transactions carry optional DATA_COLOR

`TRAN_NOTIFY_CHANGE_USER` (301), `TRAN_NOTIFY_CHAT_USER_CHANGE` (117), `TRAN_GET_USER_NAME_LIST` response entries, and user self-info replies SHALL include an additional trailing field `0x0500` (`DATA_COLOR`) when both of the following hold:

1. The receiving client is color-aware (has sent `DATA_COLOR` in its own 304).
2. The subject user's resolved color is not `0xFFFFFFFF` ("no color").

When either condition fails, these transactions SHALL NOT include field `0x0500`.

#### Scenario: Color-aware receiver gets DATA_COLOR
- **WHEN** a color-aware client receives a 301 notification about another user
- **AND** that user's resolved color is `0x00FFD700`
- **THEN** the 301 payload includes field `0x0500` with value `0x00FFD700`

#### Scenario: Legacy receiver gets no DATA_COLOR
- **WHEN** a non-color-aware client receives a 301 notification about the same user
- **THEN** the 301 payload does not include field `0x0500`
- **AND** the wire format matches the pre-change server's output for that client
