## ADDED Requirements

### Requirement: Account Editor color controls

The GUI SHALL expose color controls in the Account Editor that let an operator set, change, or clear a per-account nickname color.

The controls SHALL consist of three widgets on a single row, placed between the Name/File Root row and the Template popup row:

- An `NSColorWell` that opens the macOS system color picker.
- An `NSTextField` showing the hex representation (`#RRGGBB`), kept in sync with the color well bidirectionally.
- An `NSButton` labeled "None" (checkbox style). When checked, the color well and hex field SHALL be disabled and the account's stored color SHALL be cleared.

#### Scenario: Operator assigns a color via the well
- **WHEN** the operator opens the color picker and selects a color
- **THEN** the hex field updates to match
- **AND** on Save, the account YAML is written with `Color: "#RRGGBB"`

#### Scenario: Operator clears a color with the checkbox
- **WHEN** the operator checks the "None" checkbox
- **THEN** both the color well and hex field become disabled
- **AND** on Save, the account YAML is written without a `Color:` key

#### Scenario: Existing account without Color loads as None
- **WHEN** the operator opens an account that has no YAML `Color` key
- **THEN** the "None" checkbox is pre-checked
- **AND** the color well and hex field are disabled

### Requirement: Server Settings colored-nicknames section

The GUI SHALL expose a "Colored Nicknames" disclosure section in the left settings panel with mode selection and class default colors.

The section SHALL contain:

- A mode popup (`NSPopUpButton`) with three items: "Off", "Server-assigned only", "User choice".
- A Default Admin Color row: `NSColorWell` + hex field + "None" checkbox (same pattern as Account Editor).
- A Default Guest Color row: same widget set.

#### Scenario: Default color controls disabled in Off mode
- **WHEN** the mode popup is set to "Off"
- **THEN** both Default Admin Color and Default Guest Color rows have all three widgets disabled (but remain visible)

#### Scenario: Default color controls enabled otherwise
- **WHEN** the mode popup is set to "Server-assigned only" or "User choice"
- **THEN** both Default Admin Color and Default Guest Color rows are enabled

#### Scenario: Settings persist across GUI restarts
- **WHEN** the operator sets mode to "User choice" and default admin color to `#FFD700`, then quits and relaunches the GUI
- **THEN** the settings panel shows the same values
- **AND** the running server is configured with `Mode: user_choice` and `DefaultAdminColor: "#FFD700"` in its generated `config.yaml`
