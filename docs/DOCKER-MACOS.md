# Running Lemoniscate on macOS with Docker

While Lemoniscate can be built and run natively on macOS, Docker provides an alternative for headless deployment without Xcode or build tools.

---

## Prerequisites

1. **Install Docker Desktop for Mac**
   - Download from [docker.com/products/docker-desktop](https://www.docker.com/products/docker-desktop/)
   - Works on both Apple Silicon (M1/M2/M3) and Intel Macs
   - Open Docker Desktop and wait for it to start (whale icon in the menu bar should be steady)

2. **Open Terminal**

---

## Quick start

### 1. Get the source code

```bash
git clone https://github.com/fuzzywalrus/lemoniscate.git
cd lemoniscate
```

### 2. Build the Docker image

```bash
docker build -t lemoniscate .
```

### 3. Create a server configuration

```bash
mkdir -p ~/hotline-server
docker run --rm -v ~/hotline-server:/data lemoniscate --init -c /data/config
```

Edit `~/hotline-server/config/config.yaml` to set your server name and preferences. See `config/config.yaml.example` in the repo for all available options.

### 4. Start the server

```bash
docker run -d \
  --name hotline \
  --restart unless-stopped \
  -p 5500:5500 \
  -p 5501:5501 \
  -v ~/hotline-server:/data \
  lemoniscate
```

Connect from any Hotline client using your Mac's IP address on port 5500.

Find your IP:
```bash
ipconfig getifaddr en0
```

---

## Enabling TLS

Place your PEM certificate and key in the config directory:

```bash
docker run -d \
  --name hotline \
  --restart unless-stopped \
  -p 5500:5500 \
  -p 5501:5501 \
  -p 5600:5600 \
  -p 5601:5601 \
  -v ~/hotline-server:/data \
  lemoniscate \
  -c /data/config \
  --tls-cert /data/config/cert.pem \
  --tls-key /data/config/key.pem
```

TLS clients connect on port 5600.

---

## Enabling HOPE encryption

Add to `config.yaml`:

```yaml
EnableHOPE: true
```

Then restart: `docker restart hotline`

---

## Managing the server

```bash
docker logs hotline            # View logs
docker logs -f hotline         # Follow logs in real time
docker stop hotline            # Graceful shutdown
docker start hotline           # Start again
docker restart hotline         # Restart (after config changes)
docker rm hotline              # Remove container (data preserved)
```

---

## Shared files

Place files in `~/hotline-server/config/Files/` to make them available to connected clients.

---

## Docker vs native build

| | Docker | Native |
|---|---|---|
| Build tools required | No | Xcode or Command Line Tools |
| Bonjour (LAN discovery) | No | Yes |
| GUI admin app | No | Yes |
| TLS | Yes (OpenSSL) | Yes (SecureTransport) |
| HOPE encryption | Yes | Yes |
| macOS plist config | No (YAML only) | Yes |

If you want the GUI admin app or Bonjour discovery, use the native macOS build instead. See the main README for build instructions.

---

## Ports

| Port | Purpose |
|------|---------|
| 5500 | Hotline protocol |
| 5501 | File transfers |
| 5600 | Hotline protocol (TLS) |
| 5601 | File transfers (TLS) |
