## ADDED Requirements

### Requirement: Public chat broadcast to all connected users
The server SHALL broadcast public chat messages to all authenticated clients. Each chat message includes the sender's username.

#### Scenario: User sends a public chat message
- **WHEN** an authenticated client sends a chat transaction with message text
- **THEN** the server broadcasts the message to all connected clients with the sender's username prepended

#### Scenario: Chat message from disconnected client
- **WHEN** a client disconnects before the chat message is fully processed
- **THEN** the server does not broadcast the message

### Requirement: Private chat rooms with invite-join model
The server SHALL support private chat rooms. A user creates a private chat, receives a chat ID, and can invite other users. Invited users may join or decline.

#### Scenario: Create private chat
- **WHEN** a client sends a create-private-chat transaction
- **THEN** the server creates a new chat room, assigns a unique chat ID, adds the creator as the first member, and returns the chat ID

#### Scenario: Invite user to private chat
- **WHEN** a client sends an invite transaction with a chat ID and target user ID
- **THEN** the server sends a chat invite notification to the target user

#### Scenario: Join private chat
- **WHEN** an invited client sends a join-chat transaction with the chat ID
- **THEN** the server adds the client to the chat room and notifies existing members

#### Scenario: Leave private chat
- **WHEN** a client sends a leave-chat transaction with a chat ID
- **THEN** the server removes the client from the room and notifies remaining members

### Requirement: Private chat subject management
The server SHALL allow setting and retrieving the subject line for private chat rooms.

#### Scenario: Set chat subject
- **WHEN** a member of a private chat sends a set-subject transaction with new text
- **THEN** the server updates the subject and notifies all members of the change

#### Scenario: Get chat subject
- **WHEN** a client requests the subject for a private chat
- **THEN** the server returns the current subject text

### Requirement: Instant messages between users
The server SHALL support direct instant messages between two users. Messages are routed by target user ID.

#### Scenario: Send instant message
- **WHEN** a client sends an instant message transaction with a target user ID and message text
- **THEN** the server delivers the message to the target user if online

#### Scenario: Instant message to offline user
- **WHEN** a client sends an instant message to a user ID that is not connected
- **THEN** the server replies with an error indicating the user is not available

### Requirement: Auto-reply when user is away
The server SHALL automatically reply to instant messages on behalf of users who have set an automatic response message.

#### Scenario: Auto-reply triggered
- **WHEN** a client sends an instant message to a user who has the auto-response option set
- **THEN** the server delivers the original message AND sends the auto-response text back to the sender

### Requirement: /me action messages in chat
The server SHALL support "/me" emote-style messages in public and private chat. These are displayed differently from normal chat messages (typically without the colon separator).

#### Scenario: User sends /me action
- **WHEN** a client sends a chat message with the optional chat-action flag set
- **THEN** the server broadcasts the message formatted as an emote (e.g., "*** UserName does something")

### Requirement: Admin broadcast message
The server SHALL allow privileged users to send a broadcast message that is delivered to all connected clients as a server message.

#### Scenario: Admin sends broadcast
- **WHEN** a client with broadcast permission sends a broadcast transaction
- **THEN** the server delivers the message to every connected client as a server notification

#### Scenario: Unprivileged broadcast attempt
- **WHEN** a client without broadcast permission sends a broadcast transaction
- **THEN** the server replies with a permission-denied error
