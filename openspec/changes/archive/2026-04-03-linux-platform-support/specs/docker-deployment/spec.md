## ADDED Requirements

### Requirement: Multi-stage Dockerfile
The project SHALL include a Dockerfile that builds the Lemoniscate server binary in a build stage and produces a minimal runtime image. The runtime image SHALL contain only the server binary, shared libraries, and a volume mount point for configuration.

#### Scenario: Docker image build
- **WHEN** a user runs `docker build` on the project root
- **THEN** the build stage compiles the server binary with all dependencies and the runtime stage produces a minimal image

#### Scenario: Docker image size
- **WHEN** the Docker image is built
- **THEN** the runtime image is under 100 MB (excluding user-provided config and files)

### Requirement: Container port exposure
The Docker image SHALL expose the Hotline protocol port (default 5500), file transfer port (default 5501), and optionally the TLS ports (default 5600, 5601).

#### Scenario: Default port exposure
- **WHEN** the container starts with default configuration
- **THEN** ports 5500 and 5501 are available for host port mapping

### Requirement: Configuration and file volume mounts
The Docker image SHALL use a volume mount for the server configuration directory and file root. The server SHALL support running with `--init` to generate default configuration into the mounted volume.

#### Scenario: First run with --init
- **WHEN** the container is started with `--init` and an empty config volume
- **THEN** the server generates default configuration files into the mounted volume and exits

#### Scenario: Subsequent runs with config volume
- **WHEN** the container is started with a config volume containing valid configuration
- **THEN** the server loads the configuration and starts normally

### Requirement: Graceful shutdown in container
The server SHALL handle `SIGTERM` (Docker's stop signal) for graceful shutdown, closing all client connections and deregistering from trackers before exiting.

#### Scenario: Docker stop
- **WHEN** `docker stop` is issued against a running container
- **THEN** the server receives SIGTERM, performs graceful shutdown, and exits with code 0
