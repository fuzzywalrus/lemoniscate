## ADDED Requirements

### Requirement: ColoredNicknames config section

The server configuration SHALL support an optional top-level `ColoredNicknames` section in `config.yaml` with four keys:

- `Delivery` — one of `off`, `auto`, `always` (names per the fogWraith spec). Default: `off` when section is absent; `auto` when section is present but key is missing.
- `HonorClientColors` — boolean. Default: `false`.
- `DefaultAdminColor` — optional `"#RRGGBB"` string. Default: empty (no color).
- `DefaultGuestColor` — optional `"#RRGGBB"` string. Default: empty (no color).

When the entire section is absent, all four settings SHALL take their section-absent defaults (delivery `off`, honor-client-colors `false`, both default colors empty).

Invalid values SHALL be logged at `warn` level and replaced with defaults.

#### Scenario: Section present with all keys
- **WHEN** `config.yaml` contains
  ```yaml
  ColoredNicknames:
    Delivery: auto
    HonorClientColors: true
    DefaultAdminColor: "#FFD700"
    DefaultGuestColor: "#999999"
  ```
- **THEN** `hl_config_t.colored_nicknames.delivery == HL_CN_DELIVERY_AUTO`
- **AND** `honor_client_colors == true`
- **AND** `default_admin_color == 0x00FFD700`
- **AND** `default_guest_color == 0x00999999`

#### Scenario: Section absent
- **WHEN** `config.yaml` does not contain a `ColoredNicknames` section
- **THEN** the loaded config has delivery `off`, honor-client-colors `false`, both default colors `0xFFFFFFFF` (no color)
- **AND** no behavior change from a pre-change server

#### Scenario: Invalid delivery value
- **WHEN** `ColoredNicknames.Delivery` is `"rainbow"`
- **THEN** a warning is logged
- **AND** the effective delivery is `off`

#### Scenario: Non-bool HonorClientColors
- **WHEN** `ColoredNicknames.HonorClientColors` is `"maybe"`
- **THEN** a warning is logged
- **AND** the effective value is `false`

### Requirement: GUI plist persistence of colored-nickname settings

When the server is configured via the GUI, the GUI SHALL persist four plist keys in `com.lemoniscate.server.plist`:

- `ColoredNicknamesDelivery` (string)
- `ColoredNicknamesHonorClientColors` (bool)
- `DefaultAdminColor` (string)
- `DefaultGuestColor` (string)

Missing keys SHALL default to `off` / `false` / empty / empty respectively. The GUI's YAML generator SHALL translate these into the `ColoredNicknames:` section of the generated `config.yaml`.

#### Scenario: Plist round-trip
- **WHEN** the operator sets delivery to "Auto", honor-client-colors checked, default admin color `#FFD700`, default guest color empty via the GUI, then saves
- **THEN** `com.lemoniscate.server.plist` contains `ColoredNicknamesDelivery=auto`, `ColoredNicknamesHonorClientColors=true`, `DefaultAdminColor="#FFD700"`, `DefaultGuestColor=""`
- **AND** the generated `config.yaml` contains matching values in a `ColoredNicknames:` section
