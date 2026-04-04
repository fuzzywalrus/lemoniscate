## Context

Lemoniscate has two content storage systems that work correctly for Hotline clients:

1. **`MessageBoard.txt`** — flat text, CR-delimited, prepend-only. Each post is separated by `\r_____________________________________________\r` with a header line `"nick (date)\r\r"` before the body.
2. **`ThreadedNews.yaml`** — monolithic YAML with categories, articles, Hotline-format dates.

Mnemosyne expects structured JSON for sync. The existing news sync already works (articles are read from the in-memory `mobius_threaded_news_t` struct). The gap is message board sync — no structured post data is available.

fogWraith's Janus server uses `MessageBoard.jsonl` (structured posts) and `News/` directories (per-article JSON files). The `jsonl_message_board.c` and `dir_threaded_news.c` modules already exist in the codebase from a prior attempt.

**Key constraint (learned the hard way):** Never replace the client-facing backends. The flat text and YAML formats produce the correct Hotline wire format. Janus format readers are sync-time only.

## Goals / Non-Goals

**Goals:**
- Parse `MessageBoard.txt` into structured `{id, nick, body, timestamp}` posts at Mnemosyne sync time
- If `MessageBoard.jsonl` exists, prefer it as the sync source (already structured)
- Enable `POST /api/v1/sync/msgboard` with full and incremental modes
- Update heartbeat with actual msgboard post count
- Keep all client-facing code paths untouched

**Non-Goals:**
- Replacing the flat text or YAML backends
- Writing to `MessageBoard.jsonl` or `News/` (read-only for sync)
- Migrating file formats on startup
- Changing any Hotline wire protocol behavior

## Decisions

### 1. Parse flat text at sync time (not on startup)

**Decision:** Parse `MessageBoard.txt` only when Mnemosyne sync runs — not on startup, not cached in memory. Build the structured post list, POST to Mnemosyne, free.

**Rationale:** The message board can be large (6KB+ on Apple Media Archive, could be much larger). Parsing it on every startup wastes time when Mnemosyne might not be configured. Parsing at sync time (every 30s on first boot, then on drift check) is infrequent enough that the cost is negligible.

### 2. Prefer JSONL if present, fall back to flat text parsing

**Decision:** At sync time, check if `MessageBoard.jsonl` exists alongside `MessageBoard.txt`. If yes, use it (already structured, no parsing needed). If no, parse the flat text.

**Rationale:** Servers running Janus or a future Lemoniscate version that writes JSONL get clean structured data. Servers on the legacy format get best-effort parsing. The sync code doesn't care which source — it just needs `{id, nick, body, timestamp}` per post.

### 3. Best-effort flat text parsing

**Decision:** Split on the delimiter (`\r_____________________________________________\r`), extract nick from `"nick (date)\r\r"` header. Posts without parseable headers get `nick = "unknown"` and `timestamp = file mtime`.

**Rationale:** The flat format wasn't designed for structured data. Some posts (especially old ones or those from non-standard clients) won't have clean headers. Best-effort is acceptable for search indexing — the body text is what matters for search.

### 4. Reuse existing modules as read-only helpers

**Decision:** The existing `jsonl_message_board.c` (JSONL parser) and `dir_threaded_news.c` (directory news reader) are already in the codebase. Use them as-is for reading Janus formats when present. Don't wire them into the client path.

**Rationale:** The code works — it just shouldn't replace the client backends. Using it read-only for sync is exactly what it was designed to do (before the scope crept into backend replacement).

## Risks / Trade-offs

- **[Flat text parsing is lossy]** — Some posts may have incorrect authors or missing timestamps. Mitigation: acceptable for search indexing. The body text is the primary search target.

- **[Two codepaths for msgboard sync]** — JSONL vs flat text parsing. Mitigation: both produce the same `{id, nick, body, timestamp}` output. The sync function doesn't care about the source.

- **[Incremental hook adds complexity]** — Hooking into `flat_news_prepend` to extract a structured post for Mnemosyne. Mitigation: the prepend handler already formats the post with a header line — we parse that same format to extract nick/body.
