## ADDED Requirements

### Requirement: DATA_COLOR wire field

The server SHALL recognize and emit field ID `0x0500` (`DATA_COLOR`) as a 4-byte big-endian payload encoded as `0x00RRGGBB`, with `0xFFFFFFFF` reserved to mean "no color".

#### Scenario: Receiving DATA_COLOR in client 304
- **WHEN** a client sends `TRAN_SET_CLIENT_USER_INFO` (304) containing field `0x0500`
- **AND** the `ColoredNicknames` mode is not `off`
- **THEN** the server marks the session as color-aware
- **AND** if mode is `user_choice`, records the payload value on the connection as the client's chosen color

#### Scenario: Emitting DATA_COLOR in user notifications
- **WHEN** the server emits `TRAN_NOTIFY_CHANGE_USER` (301), `TRAN_NOTIFY_CHAT_USER_CHANGE` (117), a user self-info reply, or a `TRAN_GET_USER_NAME_LIST` response
- **AND** the receiving client is color-aware
- **AND** the subject user's resolved color is not `0xFFFFFFFF`
- **THEN** the server appends field `0x0500` with the resolved color payload

#### Scenario: Legacy client sees no DATA_COLOR
- **WHEN** a client has not announced itself as color-aware (no `DATA_COLOR` in its 304)
- **THEN** no outgoing transaction to that client contains field `0x0500`
- **AND** the wire bytes the client receives are byte-identical to the pre-change server's output

### Requirement: Color resolution cascade

The server SHALL resolve an effective nickname color for a given user by evaluating the following cascade in order and returning the first match:

1. The user's account has a YAML `Color` key with a valid color value.
2. The `ColoredNicknames` mode is `user_choice` and the user's connection has a client-sent color recorded.
3. The user's account class matches the admin template and `DefaultAdminColor` is set.
4. The user's account class matches the guest template and `DefaultGuestColor` is set.
5. Otherwise, `0xFFFFFFFF` (no color).

#### Scenario: Per-account YAML color wins
- **WHEN** an account YAML has `Color: "#FFD700"` and the config has `DefaultGuestColor: "#999999"`
- **AND** the account's access matches the guest template
- **THEN** the resolved color is `0x00FFD700` (per-account wins over class default)

#### Scenario: Client-sent color in user_choice mode
- **WHEN** mode is `user_choice`
- **AND** the user's account has no YAML `Color`
- **AND** the client sent `DATA_COLOR: 0x00FF00FF` in its 304
- **THEN** the resolved color for that user is `0x00FF00FF`

#### Scenario: Client-sent color ignored in server_only mode
- **WHEN** mode is `server_only`
- **AND** the client sent `DATA_COLOR: 0x00FF00FF` in its 304
- **AND** the account matches the admin template and `DefaultAdminColor` is `#FFD700`
- **THEN** the resolved color is `0x00FFD700` (class default wins; client value is ignored)

#### Scenario: Custom class falls through
- **WHEN** an account's access does not exactly match either the admin or guest template
- **AND** the account has no YAML `Color`
- **THEN** the cascade returns `0xFFFFFFFF` (no color)

### Requirement: Three operator modes

The server SHALL support three operator-selectable modes for colored nicknames: `off`, `server_only`, and `user_choice`.

#### Scenario: Mode `off` disables the feature entirely
- **WHEN** `ColoredNicknames.Mode` is `off` (or the section is absent)
- **THEN** no session is marked color-aware
- **AND** no outgoing transaction contains field `0x0500`
- **AND** incoming `DATA_COLOR` values in 304s are discarded without side effect

#### Scenario: Mode `server_only` gates client-sent colors
- **WHEN** mode is `server_only`
- **THEN** clients can become color-aware (the server emits `DATA_COLOR` to them)
- **AND** any `DATA_COLOR` value a client sends in its 304 is recorded on the session as "present" (for color-aware gating) but its value is not used in the cascade

#### Scenario: Mode `user_choice` honors client colors
- **WHEN** mode is `user_choice`
- **AND** a client sends `DATA_COLOR: 0x00AABBCC` in its 304
- **THEN** that value is stored on the connection
- **AND** is used in cascade step 2 when no per-account YAML color overrides

### Requirement: Account class detection

The server SHALL determine an account's class by exact match of its access bitfield against two canonical templates (`ADMIN_ACCESS_TEMPLATE` and `GUEST_ACCESS_TEMPLATE`) defined in `include/hotline/access.h`.

#### Scenario: Admin exact match
- **WHEN** `account.access == ADMIN_ACCESS_TEMPLATE`
- **THEN** the account's class is admin

#### Scenario: Guest exact match
- **WHEN** `account.access == GUEST_ACCESS_TEMPLATE`
- **THEN** the account's class is guest

#### Scenario: Any divergence is custom
- **WHEN** `account.access` differs from both templates by any bit
- **THEN** the account's class is custom (no class default applies)
