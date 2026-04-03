# Running Lemoniscate on Windows with Docker

Lemoniscate runs on Windows using Docker Desktop. The server runs inside a Linux container — no Windows build is needed.

---

## Prerequisites

1. **Install Docker Desktop for Windows**
   - Download from [docker.com/products/docker-desktop](https://www.docker.com/products/docker-desktop/)
   - Run the installer and follow the prompts
   - Docker Desktop requires Windows 10/11 with WSL 2 enabled
   - After installation, open Docker Desktop and wait for it to start (the whale icon in the system tray should be steady, not animating)

2. **Open a terminal**
   - Open **PowerShell** or **Windows Terminal** (not Command Prompt)

---

## Quick start

### 1. Get the source code

```powershell
git clone https://github.com/fuzzywalrus/lemoniscate.git
cd lemoniscate
```

### 2. Build the Docker image

```powershell
docker build -t lemoniscate .
```

This takes 1-2 minutes on the first run (it downloads Debian and compiles the server).

### 3. Create a server configuration

Create a directory on your machine for server files:

```powershell
mkdir C:\hotline-server
docker run --rm -v C:\hotline-server:/data lemoniscate --init -c /data/config
```

This creates `C:\hotline-server\config\` with default files. Open `C:\hotline-server\config\config.yaml` in Notepad (or any text editor) and set your server name:

```yaml
Name: My Hotline Server
Description: Welcome to my server!
```

See `config\config.yaml.example` in the repo for all available settings.

### 4. Start the server

```powershell
docker run -d `
  --name hotline `
  --restart unless-stopped `
  -p 5500:5500 `
  -p 5501:5501 `
  -v C:\hotline-server:/data `
  lemoniscate
```

Your server is now running. Connect from any Hotline client using your machine's IP address on port 5500.

To find your IP address:
```powershell
ipconfig
```
Look for the **IPv4 Address** under your active network adapter.

### 5. Add files to share

Place files in `C:\hotline-server\config\Files\` and they will appear in the server's file listing.

---

## Managing the server

### View logs

```powershell
docker logs hotline            # Show recent logs
docker logs -f hotline         # Follow logs in real time (Ctrl+C to stop)
```

### Stop and start

```powershell
docker stop hotline            # Graceful shutdown
docker start hotline           # Start again
```

### Restart after config changes

```powershell
docker restart hotline
```

### Remove the container

```powershell
docker stop hotline
docker rm hotline
```

Your config and files in `C:\hotline-server\` are preserved — only the container is removed.

---

## Enabling TLS

To encrypt connections with TLS, you need a PEM certificate and private key. Place them in your config directory and start the server with TLS options:

```powershell
docker run -d `
  --name hotline `
  --restart unless-stopped `
  -p 5500:5500 `
  -p 5501:5501 `
  -p 5600:5600 `
  -p 5601:5601 `
  -v C:\hotline-server:/data `
  lemoniscate `
  -c /data/config `
  --tls-cert /data/config/cert.pem `
  --tls-key /data/config/key.pem
```

Clients connect to port 5600 for TLS-encrypted connections.

---

## Enabling HOPE encryption

HOPE provides encrypted login and optional transport encryption without TLS. Add this to your `config.yaml`:

```yaml
EnableHOPE: true
```

Clients that support HOPE (such as Hotline Client 1.8+) will negotiate encryption automatically. No extra ports or certificates needed.

---

## User accounts

User accounts are stored as YAML files in `C:\hotline-server\config\Users\`. The default setup includes:

- `admin.yaml` — full permissions, no password
- `guest.yaml` — read-only permissions, no password

To set a password for the admin account, edit `admin.yaml` and change the Password field. New accounts can be created from a Hotline client with admin privileges, or by copying an existing YAML file and editing it.

---

## Ports reference

| Port | Purpose | Required |
|------|---------|----------|
| 5500 | Hotline protocol | Yes |
| 5501 | File transfers | Yes |
| 5600 | Hotline protocol (TLS) | Only if using TLS |
| 5601 | File transfers (TLS) | Only if using TLS |

---

## Firewall

If clients outside your local network need to connect, forward ports **5500** and **5501** (and **5600/5601** for TLS) on your router to your Windows machine's local IP address.

Windows Firewall may also need an inbound rule:
1. Open **Windows Defender Firewall with Advanced Security**
2. Click **Inbound Rules** > **New Rule**
3. Select **Port**, click Next
4. Enter **5500, 5501** (and 5600, 5601 for TLS)
5. Allow the connection
6. Name it "Hotline Server"

---

## Troubleshooting

**"Cannot connect to the Docker daemon"**
- Make sure Docker Desktop is running (check the system tray for the whale icon)

**Clients can't connect**
- Check that the container is running: `docker ps`
- Check logs for errors: `docker logs hotline`
- Verify your firewall allows ports 5500-5501
- Connect using your machine's LAN IP, not `localhost` (classic Hotline clients often don't resolve localhost)

**Server starts but no files show up**
- Make sure files are in `C:\hotline-server\config\Files\`, not `C:\hotline-server\Files\`

**Config changes aren't taking effect**
- Restart the container: `docker restart hotline`
