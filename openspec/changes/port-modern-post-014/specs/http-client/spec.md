## ADDED Requirements

### Requirement: Lightweight HTTP GET client
The server SHALL include a lightweight HTTP client capable of performing HTTP GET requests using raw BSD sockets, with no external library dependencies.

#### Scenario: Successful HTTP GET
- **WHEN** code performs an HTTP GET to a reachable host and path
- **THEN** the HTTP client SHALL connect via TCP, send a valid HTTP/1.1 GET request, read the response, and return the response body and status code

#### Scenario: Connection failure
- **WHEN** code performs an HTTP GET to an unreachable host
- **THEN** the HTTP client SHALL return an error status without crashing and SHALL log the connection failure

#### Scenario: Response parsing
- **WHEN** the HTTP client receives a response with chunked or content-length encoding
- **THEN** the client SHALL correctly parse the response headers and extract the complete response body

### Requirement: HTTP client PPC compatibility
The HTTP client SHALL compile and function correctly on PPC big-endian architecture with GCC 4.0.

#### Scenario: Byte order in network operations
- **WHEN** the HTTP client constructs socket addresses and parses response data
- **THEN** all network byte-order conversions SHALL use `htons`/`ntohs`/`htonl`/`ntohl` and SHALL not assume little-endian host byte order
