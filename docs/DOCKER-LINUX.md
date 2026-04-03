# Running Lemoniscate on Linux with Docker

Docker is the fastest way to run Lemoniscate on Linux without installing build dependencies.

For building from source without Docker, see [LINUX.md](LINUX.md).

---

## Prerequisites

Install Docker Engine for your distribution:

**Debian/Ubuntu:**
```bash
sudo apt install docker.io
sudo systemctl enable --now docker
sudo usermod -aG docker $USER
```
Log out and back in for the group change to take effect.

**Fedora:**
```bash
sudo dnf install docker
sudo systemctl enable --now docker
sudo usermod -aG docker $USER
```

**Arch:**
```bash
sudo pacman -S docker
sudo systemctl enable --now docker
sudo usermod -aG docker $USER
```

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

Edit `~/hotline-server/config/config.yaml`:

```bash
nano ~/hotline-server/config/config.yaml
```

See `config/config.yaml.example` in the repo for all available options.

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

Find your IP:
```bash
hostname -I | awk '{print $1}'
```

Connect from any Hotline client using that IP on port 5500.

---

## Enabling TLS

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

Generate a self-signed cert for testing:
```bash
openssl req -x509 -newkey rsa:2048 \
  -keyout ~/hotline-server/config/key.pem \
  -out ~/hotline-server/config/cert.pem \
  -days 365 -nodes -subj "/CN=hotline"
```

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
docker logs -f hotline         # Follow logs
docker stop hotline            # Graceful shutdown
docker start hotline           # Start again
docker restart hotline         # Restart (after config changes)
docker rm hotline              # Remove container (data preserved)
```

---

## Shared files

Place files in `~/hotline-server/config/Files/`.

---

## Firewall

If your server has a firewall (most cloud VPS do), open the Hotline ports:

**ufw (Ubuntu/Debian):**
```bash
sudo ufw allow 5500/tcp
sudo ufw allow 5501/tcp
```

**firewalld (Fedora/RHEL):**
```bash
sudo firewall-cmd --permanent --add-port=5500/tcp
sudo firewall-cmd --permanent --add-port=5501/tcp
sudo firewall-cmd --reload
```

Add 5600 and 5601 if using TLS.

---

## Docker Compose (optional)

Create `docker-compose.yml`:

```yaml
services:
  hotline:
    build: .
    container_name: hotline
    restart: unless-stopped
    ports:
      - "5500:5500"
      - "5501:5501"
    volumes:
      - ./hotline-data:/data
    command: ["-c", "/data/config"]
```

Then:

```bash
docker compose up -d
docker compose logs -f
```

---

## Ports

| Port | Purpose |
|------|---------|
| 5500 | Hotline protocol |
| 5501 | File transfers |
| 5600 | Hotline protocol (TLS) |
| 5601 | File transfers (TLS) |
