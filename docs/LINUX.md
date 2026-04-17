# Running Lemoniscate on Linux

Lemoniscate runs natively on Linux as a headless CLI server, or inside a Docker container.

---

## Option A: Docker (Recommended)

Docker is the easiest way to run Lemoniscate on Linux, macOS, or Windows. No compiler or dependencies needed.

### 1. Build the image

```bash
git clone https://github.com/fuzzywalrus/lemoniscate.git
cd lemoniscate
docker build -t lemoniscate .
```

### 2. Create a config directory

```bash
mkdir -p ~/hotline-server
docker run --rm -v ~/hotline-server:/data lemoniscate --init -c /data/config
```

This creates `~/hotline-server/config/` with default configuration files:

```
config/
  config.yaml          Server settings
  Agreement.txt        Agreement text shown on connect
  MessageBoard.txt     Message board content
  Banlist.yaml         IP/user ban list
  ThreadedNews.yaml    Threaded news categories
  Files/               Shared file directory
  Users/               User accounts (one YAML file per account)
    admin.yaml         Default admin (no password)
    guest.yaml         Default guest
```

### 3. Edit the configuration

Open `~/hotline-server/config/config.yaml` in a text editor. See [`config/config.yaml.example`](../config/config.yaml.example) in the repo for the full annotated template — every option is documented inline, including the optional `ChatHistory:` section (persistent public chat with cursor pagination), TLS / HOPE encryption, Mnemosyne search sync, and per-account permissions.

At minimum, set your server name:

```yaml
Name: My Hotline Server
Description: Welcome!
```

To turn on persistent chat history (opt-in), append:

```yaml
ChatHistory:
  Enabled: true
  MaxMessages: 10000      # per-channel cap; 0 = unlimited
  MaxDays: 30             # age cap in days; 0 = unlimited
  LegacyBroadcast: true   # replay recent chat to non-capable clients on join
  LegacyCount: 30
  LogJoins: false         # record sign-on / sign-off as system messages
```

Storage lands at `<FileRoot>/ChatHistory/channel-N.jsonl`.

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

Your server is now running on port 5500. Connect from any Hotline client.

### 5. Enable TLS (optional)

Place your PEM certificate and key in the config directory, then start with TLS flags:

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

Clients connect to port 5600 for TLS.

### Managing the server

```bash
docker logs hotline          # View logs
docker logs -f hotline       # Follow logs in real time
docker stop hotline          # Graceful shutdown
docker start hotline         # Start again
docker rm hotline            # Remove container
```

### Ports

| Port | Protocol | Purpose |
|------|----------|---------|
| 5500 | TCP | Hotline protocol |
| 5501 | TCP | File transfers |
| 5600 | TCP | Hotline protocol (TLS) |
| 5601 | TCP | File transfers (TLS) |

---

## Option B: Build from source

### Prerequisites

Debian/Ubuntu:
```bash
sudo apt install build-essential libssl-dev libyaml-dev
```

Fedora/RHEL:
```bash
sudo dnf install gcc make openssl-devel libyaml-devel
```

Arch:
```bash
sudo pacman -S base-devel openssl libyaml
```

### Build

```bash
git clone https://github.com/fuzzywalrus/lemoniscate.git
cd lemoniscate
make
```

This produces a single binary: `lemoniscate`

### Initialize and run

```bash
# Create default config
./lemoniscate --init -c ~/hotline-server

# Edit config
nano ~/hotline-server/config.yaml

# Start the server
./lemoniscate -c ~/hotline-server
```

### Command-line options

```
-i, --interface ADDR   IP address to listen on (default: all interfaces)
-p, --port PORT        Base port (default: 5500)
-c, --config DIR       Configuration directory
-f, --log-file PATH    Log to a file instead of stderr
-l, --log-level LEVEL  debug, info, or error (default: info)
    --tls-cert PATH    PEM certificate for TLS
    --tls-key PATH     PEM private key for TLS
    --tls-port PORT    TLS base port (default: base port + 100)
    --init             Create default config directory and exit
-v, --version          Print version
-h, --help             Show help
```

### Run as a systemd service

Create `/etc/systemd/system/lemoniscate.service`:

```ini
[Unit]
Description=Lemoniscate Hotline Server
After=network.target

[Service]
Type=simple
User=hotline
ExecStart=/usr/local/bin/lemoniscate -c /etc/lemoniscate
Restart=on-failure
RestartSec=5

[Install]
WantedBy=multi-user.target
```

Then:

```bash
sudo cp lemoniscate /usr/local/bin/
sudo mkdir -p /etc/lemoniscate
sudo ./lemoniscate --init -c /etc/lemoniscate
sudo useradd -r -s /usr/sbin/nologin hotline
sudo chown -R hotline: /etc/lemoniscate
sudo systemctl enable --now lemoniscate
```

---

## Platform notes

- **Bonjour** is not available on Linux. The `EnableBonjour` config option is silently ignored.
- **Plist configuration** is macOS-only. Linux uses YAML configuration exclusively.
- **HOPE encryption** works identically on Linux and macOS.
- **TLS** uses OpenSSL on Linux (SecureTransport on macOS). Both accept standard PEM files.
- **MacRoman encoding** is fully supported for classic Mac OS clients.
