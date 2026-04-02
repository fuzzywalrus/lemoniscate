## ADDED Requirements

### Requirement: kqueue-based event loop for connection handling
The server SHALL use a kqueue-based event loop to handle client connections on the protocol port and file transfer port. New connections are accepted and registered with the event loop for non-blocking I/O.

#### Scenario: Accept new connection
- **WHEN** a client connects to the protocol port
- **THEN** the server accepts the connection, creates a client connection state object, and registers the socket with the kqueue event loop

#### Scenario: Concurrent connections
- **WHEN** multiple clients connect simultaneously
- **THEN** the server handles all connections concurrently via the kqueue event loop without blocking

### Requirement: Tracker registration via UDP heartbeat
The server SHALL register with configured Hotline trackers by sending periodic UDP packets containing the server name, description, user count, and protocol version (190). The registration interval is defined by the tracker configuration.

#### Scenario: Tracker heartbeat sent
- **WHEN** the server has trackers configured and the heartbeat interval elapses
- **THEN** the server sends a UDP registration packet to each configured tracker with current server metadata

#### Scenario: No trackers configured
- **WHEN** the server has no tracker entries in its configuration
- **THEN** no tracker registration packets are sent

### Requirement: Bonjour/mDNS local network discovery
The server SHALL register itself via Bonjour (DNS-SD) for local network discovery, allowing Hotline clients on the same network to find the server without manual IP entry.

#### Scenario: Bonjour registration
- **WHEN** the server starts with Bonjour enabled
- **THEN** it registers a DNS-SD service of type "_hotline._tcp" with the configured server name and port

#### Scenario: Bonjour service removed on shutdown
- **WHEN** the server shuts down
- **THEN** the Bonjour service registration is removed

### Requirement: Per-IP rate limiting with token bucket
The server SHALL enforce per-IP rate limiting using a token-bucket algorithm to prevent connection abuse. Connections from IPs that exceed the rate limit are rejected.

#### Scenario: Normal connection rate
- **WHEN** a client IP connects at a rate within the configured limit
- **THEN** the server accepts all connections normally

#### Scenario: Rate limit exceeded
- **WHEN** a client IP exceeds the configured connection rate
- **THEN** the server rejects new connections from that IP until tokens replenish

### Requirement: Idle client disconnect after timeout
The server SHALL track client activity and disconnect clients that have been idle beyond the configured timeout (default 5 minutes). Idle detection resets on any transaction received from the client.

#### Scenario: Client goes idle
- **WHEN** a connected client sends no transactions for longer than the idle timeout
- **THEN** the server disconnects the client and broadcasts a user-left notification

#### Scenario: Client activity resets idle timer
- **WHEN** a client sends any transaction
- **THEN** the idle timer for that client resets to zero

### Requirement: Dual-port architecture
The server SHALL listen on two TCP ports: a protocol port (default 5500) for Hotline transactions and a transfer port (default 5501) for file transfer data. Both ports are configurable.

#### Scenario: Protocol port accepts transactions
- **WHEN** a client connects to the protocol port
- **THEN** the server expects a Hotline handshake followed by transaction traffic

#### Scenario: Transfer port accepts file data
- **WHEN** a client connects to the transfer port with a valid transfer reference number
- **THEN** the server looks up the transfer record and begins the file data exchange

#### Scenario: Transfer port with invalid reference
- **WHEN** a client connects to the transfer port with an unknown reference number
- **THEN** the server closes the connection

### Requirement: IP ban enforcement
The server SHALL enforce an IP-based ban list. Connections from banned IP addresses are rejected before the handshake phase.

#### Scenario: Banned IP attempts connection
- **WHEN** a client connects from a banned IP address
- **THEN** the server closes the connection immediately

#### Scenario: Non-banned IP connects normally
- **WHEN** a client connects from an IP not on the ban list
- **THEN** the server proceeds with the normal handshake flow
