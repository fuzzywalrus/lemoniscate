## Context

Lemoniscate inherits two storage formats from Mobius:

1. **`MessageBoard.txt`** — flat text, CR-delimited, prepend-only. No post IDs, no timestamps, no author attribution. Posts are separated by a configurable delimiter string. This format makes Mnemosyne message board sync impossible because the API expects `{id, nick, body, timestamp}`.

2. **`ThreadedNews.yaml`** — single YAML file containing all categories, bundles, and articles. Every article post rewrites the entire file. Articles have IDs, titles, posters, and Hotline-format dates, but the monolithic file doesn't scale.

fogWraith's Janus server uses a different approach (examined from a live Janus config):

- **`MessageBoard.jsonl`** — one JSON object per line: `{"id":1,"login":"","nick":"fogWraith","body":"Welcome!","ts":"2026-03-19T16:01:41Z"}`
- **`News/`** directory tree — `_meta.json` per category/bundle (name, kind, guid), numbered `NNNN.json` per article (id, title, poster, date, parent_id, body)

## Goals / Non-Goals

**Goals:**
- Implement JSONL message board storage with structured posts
- Implement directory-based threaded news with per-article JSON files
- Auto-migrate from old formats on startup (no manual steps)
- Enable Mnemosyne message board sync (`POST /api/v1/sync/msgboard`)
- Cross-compatibility: a Janus config directory can be read by Lemoniscate and vice versa

**Non-Goals:**
- Changing the Hotline wire protocol (clients still see the same message board text and news articles)
- Supporting concurrent writes from multiple server processes (single-process model)
- Implementing a database backend (files are the storage layer)
- Backward-writing: Lemoniscate won't write back to `ThreadedNews.yaml` or `MessageBoard.txt` after migration

## Decisions

### 1. JSONL for message board (not JSON array, not SQLite)

**Decision:** Store message board posts as newline-delimited JSON (JSONL). Each line is a complete `{id, nick, login, body, ts}` object.

**Rationale:** JSONL is append-only friendly (new posts = append a line), doesn't require parsing the entire file to add a post, and matches Janus's format exactly. A JSON array would require rewriting the file on every post. SQLite would add a dependency.

### 2. Directory tree for threaded news (matching Janus layout)

**Decision:** Store each category as a directory containing `_meta.json` + numbered article files. Bundles are directories that contain subcategory directories.

**Rationale:** Per-article files mean posting/deleting an article only touches one file. The `_meta.json` stores category metadata (name, kind, guid, add_sn, delete_sn). This matches Janus's format exactly and scales to thousands of articles without monolithic file rewrites.

**Layout:**
```
News/
  Guestbook/
    _meta.json              {"name":"Guestbook","kind":"category","guid":[...],"add_sn":0,"delete_sn":0}
    0001.json               {"id":1,"title":"Hello","poster":"fogWraith","date":"2026-03-19T...","parent_id":0,"body":"..."}
    0002.json
  Pantheon/                 (bundle — contains subcategories)
    _meta.json              {"name":"Pantheon","kind":"bundle",...}
    Janus/
      _meta.json            {"name":"Janus","kind":"category",...}
      0001.json
```

### 3. Auto-migration on startup with backup

**Decision:** On startup, detect old formats and migrate automatically. Rename old files with `.legacy` suffix (not delete).

**Rationale:** Operators shouldn't need to run migration scripts. The server detects format by checking for `MessageBoard.jsonl` (new) vs `MessageBoard.txt` (old), and `News/` directory (new) vs `ThreadedNews.yaml` (old). Old files are preserved as `.legacy` for rollback.

**Migration logic:**
- If `MessageBoard.jsonl` exists → use it (new format)
- Else if `MessageBoard.txt` exists → parse into structured posts, write `MessageBoard.jsonl`, rename old file to `MessageBoard.txt.legacy`
- If `News/` directory exists → use it (new format)
- Else if `ThreadedNews.yaml` exists → convert categories/articles to directory tree, rename old file to `ThreadedNews.yaml.legacy`

### 4. Message board text parsing heuristic

**Decision:** Parse `MessageBoard.txt` by splitting on the configured delimiter (default: `\r__________\r`) and extracting timestamps/nicks from the formatted header lines when possible. Posts without parseable headers get auto-generated IDs and timestamps.

**Rationale:** The flat format wasn't designed for structured data. We do best-effort parsing — the delimiter separates posts, and Mobius/Lemoniscate prepend a header like `"From nick (date):\r"` before the body. If parsing fails, the entire post becomes the body with a generic author.

### 5. Vtable interface for both backends

**Decision:** Keep the existing vtable interfaces (`mobius_flat_news_t` for message board, `mobius_threaded_news_t` for news) but add new constructors: `mobius_jsonl_news_new()` and `mobius_dir_news_new()`. The server picks the right constructor based on format detection.

**Rationale:** The handler layer doesn't change. The vtable abstracts the storage backend. This also means we can keep the old YAML/text backends for testing or fallback.

## Risks / Trade-offs

- **[Message board parsing is lossy]** — The flat text format doesn't reliably encode post boundaries or metadata. Migration is best-effort. Some posts may have incorrect authors or timestamps. Mitigation: preserve `MessageBoard.txt.legacy` for manual inspection.

- **[Directory tree has more files on disk]** — A server with 1,000 articles creates 1,000+ files vs one YAML file. Mitigation: filesystems handle this fine; each file is tiny (<10KB). This is actually better for SSHFS/NFS since operations are per-file, not full-file-rewrite.

- **[GUID generation for migrated categories]** — Janus categories have 16-byte GUIDs. Migrated categories from YAML already have GUIDs (stored in the YAML). New categories created by Lemoniscate need GUID generation. Mitigation: generate random 16-byte GUIDs from `/dev/urandom`.

- **[Concurrent file access on SSHFS]** — Writing per-article JSON files over SSHFS is slower than local disk but faster than rewriting a monolithic YAML. Each write is small and atomic (write-tmp-rename pattern).
