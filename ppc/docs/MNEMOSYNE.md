# Mnemosyne Search Integration

Mnemosyne is a search engine for Hotline servers. When enabled, Lemoniscate pushes your file listings and news articles to a Mnemosyne instance so clients like [Hotline Navigator](https://hotlinenavigator.com/) can search across servers without connecting to each one individually.

The main public Mnemosyne instance is run by VesperNet at `tracker.vespernet.net:8980`. You can search it at [agora.vespernet.net/search](https://agora.vespernet.net/search).

---

## Quick Start

### 1. Get an API key

1. Go to [agora.vespernet.net/login](https://agora.vespernet.net/login)
2. Register for an account (you'll get an `mop_`-prefixed operator key)
3. Once approved, add your server through the Agora portal
4. You'll receive an `msv_`-prefixed server API key (e.g., `msv_7fff661a...`)

The `mop_` key is your operator login. The `msv_` key is what your server uses to authenticate with Mnemosyne. Keep both safe.

### 2. Add to config.yaml

```yaml
Mnemosyne:
  url: http://tracker.vespernet.net:8980
  api_key: msv_your_key_here
  index_files: true
  index_news: true
```

### 3. Restart the server

The server will sync automatically:
- **30 seconds after startup**: Full sync of all files and news
- **Every 5 minutes**: Heartbeat with server name and content counts
- **On file upload/delete or news post**: Incremental updates pushed immediately
- **Every 15 minutes**: Drift check — if file counts don't match, triggers a resync
- **On shutdown**: Deregisters from Mnemosyne

---

## What Gets Indexed

| Content Type | What's Sent | Searchable By |
|-------------|-------------|---------------|
| **Files** | Path, name, size, modified date | File name, path |
| **News** | Title, body, poster, date, category | Title, body text, poster name |
| **Message Board** | Not yet supported | — |

Message board sync is not implemented because the flat `MessageBoard.txt` format doesn't have structured post data (no IDs, no per-post dates). This may be added in a future release.

---

## Config Options

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `url` | string | — | Mnemosyne instance URL. Required to enable sync. |
| `api_key` | string | — | Server API key (`msv_` prefix). Required if `url` is set. |
| `index_files` | bool | `true` | Sync file listings to Mnemosyne |
| `index_news` | bool | `true` | Sync threaded news articles to Mnemosyne |

If `url` is set but `api_key` is missing, sync is disabled with a warning in the log.

---

## How It Works

Lemoniscate uses the Mnemosyne sync API to push content:

```
POST /api/v1/sync/heartbeat     — server name, description, content counts
POST /api/v1/sync/files         — full file listing (mode: "full")
POST /api/v1/sync/news          — full news listing (mode: "full")
POST /api/v1/sync/files         — incremental adds/removes (mode: "incremental")
POST /api/v1/sync/news          — incremental article adds (mode: "incremental")
POST /api/v1/sync/deregister    — remove server from index
```

All requests use `?api_key=msv_...` query parameter for authentication. Content-Type is `application/json`.

The full sync collects all files and news into a single JSON payload and sends it in one POST. For large servers (thousands of files), this may take a few seconds over a slow connection but the payload is typically well under 10MB.

---

## Resilience

Lemoniscate handles Mnemosyne outages gracefully:

- **Exponential backoff**: If a request fails, retries are delayed (2s, 4s, 8s, 16s, 32s)
- **Suspension**: After 5 consecutive failures, all sync stops. The heartbeat timer continues probing every 5 minutes.
- **Auto-recovery**: When a heartbeat succeeds after suspension, sync resumes with a fresh full sync.
- **No impact on server**: Sync runs in the main event loop but never blocks client operations. Failures are logged and ignored.

The log output is designed to not spam. You'll see one line per backoff escalation, one line on suspension, one on recovery.

---

## Checking Your Index

### From the command line

```bash
# Check stats (how many servers/files are indexed)
curl -s http://tracker.vespernet.net:8980/api/v1/stats | python3 -m json.tool

# Search for your content
curl -s "http://tracker.vespernet.net:8980/api/v1/search?q=your+search+term&limit=10" | python3 -m json.tool

# Filter by type
curl -s "http://tracker.vespernet.net:8980/api/v1/search?q=test&type=files&limit=10" | python3 -m json.tool
curl -s "http://tracker.vespernet.net:8980/api/v1/search?q=test&type=news&limit=10" | python3 -m json.tool
```

### From Hotline Navigator

Mnemosyne search is built into Hotline Navigator. The default VesperNet instance is pre-configured — just click the search tab and type.

### From the web

Visit [agora.vespernet.net/search](https://agora.vespernet.net/search) to search all indexed servers.

---

## Troubleshooting

**"Mnemosyne sync enabled" but nothing indexed**

Check the log for errors after the 30-second startup delay. Common issues:
- `status 401` — Invalid API key. Double-check your `msv_` key.
- `status -1` — Can't connect. Check that the URL is correct and the server can reach `tracker.vespernet.net` on port 8980.
- No sync messages at all — The `Mnemosyne` section may not be parsed. Check YAML indentation (2 spaces, nested under `Mnemosyne:`).

**Files synced but not appearing in search**

The Mnemosyne server indexes content after receiving the full sync POST. It may take a few seconds. If files still don't appear, try searching by exact filename.

**Sync suspended**

Look for "sync suspended after 5 failures" in the log. This means Mnemosyne was unreachable. The heartbeat will keep probing — sync resumes automatically when connectivity returns. No action needed.

**Server appears but with 0 files**

This was a known issue with earlier versions (<0.1.5) that used incorrect JSON field names. Update to 0.1.5 or later.

---

## Running Your Own Mnemosyne Instance

Mnemosyne is a separate service maintained by the VesperNet project. If you want to run a private instance instead of using the public one, check the Mnemosyne documentation. The `url` config option can point to any Mnemosyne-compatible server.

---

## Docker

When running Lemoniscate in Docker, the Mnemosyne config works the same way — just set it in `config.yaml` inside your mounted config volume:

```bash
docker run -d \
  -v ~/hotline-server:/data \
  -p 5500:5500 -p 5501:5501 \
  lemoniscate -c /data/config
```

The container needs outbound HTTP access to `tracker.vespernet.net:8980`. No special Docker networking is required.
