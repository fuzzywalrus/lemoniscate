## MODIFIED Requirements

### Requirement: kqueue-based event loop for connection handling
The server SHALL use a platform-abstracted event loop to handle client connections on the protocol port and file transfer port. On macOS, the backend SHALL use kqueue. On Linux, the backend SHALL use epoll. New connections are accepted and registered with the event loop for non-blocking I/O.

#### Scenario: Accept new connection
- **WHEN** a client connects to the protocol port
- **THEN** the server accepts the connection, creates a client connection state object, and registers the socket with the platform event loop

#### Scenario: Concurrent connections
- **WHEN** multiple clients connect simultaneously
- **THEN** the server handles all connections concurrently via the platform event loop without blocking

#### Scenario: Event loop behavior on Linux
- **WHEN** the server runs on Linux
- **THEN** epoll provides identical connection handling behavior to kqueue on macOS
