## ADDED Requirements

### Requirement: ColoredNicknames config section

The server configuration SHALL support an optional top-level `ColoredNicknames` section in `config.yaml` with three keys:

- `Mode` — one of `off`, `server_only`, `user_choice`. Default: `off`.
- `DefaultAdminColor` — optional `"#RRGGBB"` string. Default: empty (no color).
- `DefaultGuestColor` — optional `"#RRGGBB"` string. Default: empty (no color).

When the entire section is absent, all three settings SHALL take their defaults (mode `off`, both defaults empty).

Invalid values SHALL be logged at `warn` level and replaced with defaults.

#### Scenario: Section present with all three keys
- **WHEN** `config.yaml` contains
  ```yaml
  ColoredNicknames:
    Mode: user_choice
    DefaultAdminColor: "#FFD700"
    DefaultGuestColor: "#999999"
  ```
- **THEN** `hl_config_t.colored_nicknames.mode == HL_CN_USER_CHOICE`
- **AND** `default_admin_color == 0x00FFD700`
- **AND** `default_guest_color == 0x00999999`

#### Scenario: Section absent
- **WHEN** `config.yaml` does not contain a `ColoredNicknames` section
- **THEN** the loaded config has mode `off`, both default colors `0xFFFFFFFF` (no color)
- **AND** no behavior change from a pre-change server

#### Scenario: Invalid mode value
- **WHEN** `ColoredNicknames.Mode` is `"rainbow"`
- **THEN** a warning is logged
- **AND** the effective mode is `off`

### Requirement: GUI plist persistence of colored-nickname settings

When the server is configured via the GUI, the GUI SHALL persist three plist keys in `com.lemoniscate.server.plist`:

- `ColoredNicknamesMode` (string)
- `DefaultAdminColor` (string)
- `DefaultGuestColor` (string)

Missing keys SHALL default to `off` / empty / empty respectively. The GUI's YAML generator SHALL translate these into the `ColoredNicknames:` section of the generated `config.yaml`.

#### Scenario: Plist round-trip
- **WHEN** the operator sets mode to "User choice", default admin color `#FFD700`, default guest color empty via the GUI, then saves
- **THEN** `com.lemoniscate.server.plist` contains `ColoredNicknamesMode=user_choice`, `DefaultAdminColor="#FFD700"`, `DefaultGuestColor=""`
- **AND** the generated `config.yaml` contains matching values in a `ColoredNicknames:` section
