## ADDED Requirements

### Requirement: Account CRUD with YAML persistence
The server SHALL manage user accounts as individual YAML files in the configured Users directory. Each account file stores the username, password hash, access permissions, and display name.

#### Scenario: Create new account
- **WHEN** a privileged client sends a create-account transaction with username, password, and access bits
- **THEN** the server creates a new YAML file in the Users directory and the account becomes immediately available for login

#### Scenario: Read account details
- **WHEN** a privileged client sends a get-account transaction for an existing username
- **THEN** the server returns the account's access permissions, display name, and login name

#### Scenario: Update existing account
- **WHEN** a privileged client sends a modify-account transaction with updated fields
- **THEN** the server updates the account's YAML file and applies changes immediately

#### Scenario: Delete account
- **WHEN** a privileged client sends a delete-account transaction for an existing username
- **THEN** the server removes the account YAML file and the account can no longer be used for login

### Requirement: 41-bit granular permission system
The server SHALL enforce a 41-bit access control system where each bit controls a specific privilege (e.g., download files, upload files, create chat, read news, send messages, disconnect users, etc.). Permissions are evaluated per-transaction based on the authenticated user's access bits.

#### Scenario: Permitted operation
- **WHEN** a client sends a transaction and the client's account has the corresponding access bit set
- **THEN** the server processes the transaction normally

#### Scenario: Denied operation
- **WHEN** a client sends a transaction requiring a permission bit that is not set on the client's account
- **THEN** the server replies with a permission-denied error and does not execute the operation

### Requirement: User list retrieval
The server SHALL provide a list of all currently connected users. Each entry includes the user's client ID, icon ID, user flags (away, admin, refuse PM, refuse chat), and display name.

#### Scenario: Get user list
- **WHEN** a client sends a get-user-list transaction
- **THEN** the server returns a list of all connected users with their ID, icon, flags, and name

### Requirement: User info retrieval
The server SHALL provide detailed information about a specific connected user, including their username, client version, idle time, and any auto-reply message.

#### Scenario: Get user info
- **WHEN** a client sends a get-user-info transaction with a valid client ID
- **THEN** the server returns the user's detailed information

### Requirement: User flag management
Each connected user SHALL have flag bits that indicate state: away (bit 0), admin (bit 1), refuse private messages (bit 2), and refuse private chat (bit 3). Flags are included in user list entries and updated in real-time.

#### Scenario: User sets away flag
- **WHEN** a client sets user options with the away flag
- **THEN** the server updates the user's flags and notifies other clients of the change

#### Scenario: User refuses private messages
- **WHEN** a client sets the refuse-PM flag
- **THEN** the server blocks instant message delivery to that client and informs the sender

### Requirement: Kick and disconnect users
The server SHALL allow privileged users to forcibly disconnect other clients from the server.

#### Scenario: Admin disconnects a user
- **WHEN** a client with disconnect-user permission sends a disconnect transaction with a target client ID
- **THEN** the server forcibly disconnects the target client and notifies remaining users

#### Scenario: Disconnect without permission
- **WHEN** a client without disconnect permission attempts to disconnect another user
- **THEN** the server replies with a permission-denied error

### Requirement: Account list retrieval
The server SHALL allow privileged users to retrieve a list of all configured accounts (not just connected users).

#### Scenario: List all accounts
- **WHEN** a client with account-management permission sends a get-account-list transaction
- **THEN** the server returns a list of all account names from the Users directory

### Requirement: User change notifications
The server SHALL broadcast user-change notifications when a user connects, disconnects, or changes their name/icon/flags, so all clients can maintain an up-to-date user list.

#### Scenario: User connects
- **WHEN** a new client completes authentication
- **THEN** the server broadcasts a user-joined notification to all other connected clients

#### Scenario: User disconnects
- **WHEN** a client disconnects (gracefully or by timeout)
- **THEN** the server broadcasts a user-left notification to all other connected clients

#### Scenario: User changes name or icon
- **WHEN** a client sends a set-user-info transaction changing their display name or icon
- **THEN** the server broadcasts a user-changed notification with the updated information
