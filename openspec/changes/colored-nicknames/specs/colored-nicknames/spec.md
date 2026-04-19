## ADDED Requirements

### Requirement: DATA_COLOR wire field

The server SHALL recognize and emit field ID `0x0500` (`DATA_COLOR`) as a 4-byte big-endian payload encoded as `0x00RRGGBB`, with `0xFFFFFFFF` reserved to mean "no color".

#### Scenario: Receiving DATA_COLOR in client 304
- **WHEN** a client sends `TRAN_SET_CLIENT_USER_INFO` (304) containing field `0x0500`
- **AND** `ColoredNicknames.Delivery` is not `off`
- **THEN** the server marks the session as color-aware
- **AND** if `ColoredNicknames.HonorClientColors` is `true`, records the payload value on the connection as the client's chosen color

#### Scenario: Emitting DATA_COLOR in user notifications
- **WHEN** the server emits `TRAN_NOTIFY_CHANGE_USER` (301), `TRAN_NOTIFY_CHAT_USER_CHANGE` (117), a user self-info reply, or a `TRAN_GET_USER_NAME_LIST` response
- **AND** the subject user's resolved color is not `0xFFFFFFFF`
- **AND** either `Delivery == always` OR (`Delivery == auto` AND the receiving client is color-aware)
- **THEN** the server appends field `0x0500` with the resolved color payload

#### Scenario: Legacy client sees no DATA_COLOR in auto mode
- **WHEN** `Delivery == auto`
- **AND** a client has not announced itself as color-aware (no `DATA_COLOR` in its 304)
- **THEN** no outgoing transaction to that client contains field `0x0500`
- **AND** the wire bytes the client receives are byte-identical to the pre-change server's output

#### Scenario: Always mode sends to legacy clients too
- **WHEN** `Delivery == always`
- **AND** a legacy client connects (has not sent `DATA_COLOR` in its 304)
- **THEN** outgoing user notifications to that client include field `0x0500` when the subject has a resolved color
- **AND** the legacy client ignores the trailing field per the spec and remains connected

### Requirement: Color resolution cascade

The server SHALL resolve an effective nickname color for a given user by evaluating the following cascade in order and returning the first match:

1. The user's account has a YAML `Color` key with a valid color value.
2. `ColoredNicknames.HonorClientColors` is `true` AND the user's connection has a client-sent color recorded.
3. The user's account class matches the admin template AND `DefaultAdminColor` is set.
4. The user's account class matches the guest template AND `DefaultGuestColor` is set.
5. Otherwise, `0xFFFFFFFF` (no color).

#### Scenario: Per-account YAML color wins
- **WHEN** an account YAML has `Color: "#FFD700"` and the config has `DefaultGuestColor: "#999999"`
- **AND** the account's access matches the guest template
- **THEN** the resolved color is `0x00FFD700` (per-account wins over class default)

#### Scenario: Client-sent color honored when enabled
- **WHEN** `HonorClientColors == true`
- **AND** the user's account has no YAML `Color`
- **AND** the client sent `DATA_COLOR: 0x00FF00FF` in its 304
- **THEN** the resolved color for that user is `0x00FF00FF`

#### Scenario: Client-sent color ignored when disabled
- **WHEN** `HonorClientColors == false`
- **AND** the client sent `DATA_COLOR: 0x00FF00FF` in its 304
- **AND** the account matches the admin template and `DefaultAdminColor` is `#FFD700`
- **THEN** the resolved color is `0x00FFD700` (class default wins; client value is ignored even though the session is marked color-aware)

#### Scenario: Custom class falls through
- **WHEN** an account's access does not exactly match either the admin or guest template
- **AND** the account has no YAML `Color`
- **THEN** the cascade returns `0xFFFFFFFF` (no color)

### Requirement: Delivery gating

The server SHALL support three operator-selectable `Delivery` values (names per the fogWraith spec): `off`, `auto`, and `always`. The value controls when `DATA_COLOR` is emitted in outgoing transactions.

#### Scenario: Delivery `off` disables the feature entirely
- **WHEN** `ColoredNicknames.Delivery` is `off` (or the section is absent)
- **THEN** no session is marked color-aware
- **AND** no outgoing transaction contains field `0x0500`
- **AND** incoming `DATA_COLOR` values in 304s are discarded without side effect

#### Scenario: Delivery `auto` requires client opt-in
- **WHEN** `Delivery` is `auto`
- **THEN** outgoing `DATA_COLOR` is included only for clients marked color-aware (they opted in by sending `DATA_COLOR` in their 304)
- **AND** clients that never sent `DATA_COLOR` receive the same wire format as the pre-change server

#### Scenario: Delivery `always` ignores opt-in
- **WHEN** `Delivery` is `always`
- **THEN** outgoing `DATA_COLOR` is included for every client when the subject has a resolved color, regardless of whether the receiver is color-aware

### Requirement: Client-color input gating

The server SHALL support a boolean `HonorClientColors` that controls whether client-sent `DATA_COLOR` values enter the cascade.

#### Scenario: HonorClientColors false — client value recorded but not used
- **WHEN** `HonorClientColors == false`
- **AND** a client sends `DATA_COLOR: 0x00AABBCC` in its 304
- **THEN** the server marks the session color-aware (for auto-delivery purposes)
- **AND** cascade step 2 is skipped; the client-sent value does not affect the resolved color

#### Scenario: HonorClientColors true — client value enters cascade
- **WHEN** `HonorClientColors == true`
- **AND** a client sends `DATA_COLOR: 0x00AABBCC` in its 304
- **AND** the account has no YAML `Color`
- **THEN** the resolved color for that user is `0x00AABBCC`

#### Scenario: Delivery off short-circuits HonorClientColors
- **WHEN** `Delivery == off`
- **AND** `HonorClientColors == true`
- **AND** a client sends `DATA_COLOR` in its 304
- **THEN** the value is not recorded on the connection
- **AND** no cascade tier is computed because no outgoing transaction contains the field

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
