## Why

Lemoniscate stores threaded news in a single monolithic `ThreadedNews.yaml` file and the message board as a flat prepend-only text file (`MessageBoard.txt`). These formats are inherited from Mobius and have limitations: the YAML file doesn't scale (entire file must be loaded/saved for every operation), the message board has no structured post data (no IDs, no per-post timestamps, no author tracking), and neither format is compatible with fogWraith's Janus server — the emerging reference implementation for modern Hotline.

Adopting Janus-compatible storage enables: structured message board posts (unlocking Mnemosyne message board sync, which is currently blocked), per-article JSON files that don't require rewriting the entire news tree on every post, and cross-compatibility with Janus server configurations.

**Reference format (from Janus):**
- Message board: `MessageBoard.jsonl` — one JSON object per line: `{id, login, nick, body, ts}`
- Threaded news: directory tree under `News/` — `_meta.json` per category/bundle, `NNNN.json` per article: `{id, title, poster, date, parent_id, body}`

## What Changes

- Add a JSONL message board backend that stores posts as structured `{id, nick, login, body, ts}` objects, one per line. Supports append, read-all, and individual post access.
- Add a directory-based threaded news backend where each category is a directory with `_meta.json` metadata and numbered `NNNN.json` article files. Bundles (news folders containing subcategories) are nested directories.
- Add migration logic: on startup, if `MessageBoard.txt` exists without `MessageBoard.jsonl`, parse the flat text into structured posts. If `ThreadedNews.yaml` exists without a `News/` directory, convert YAML articles to JSON files.
- Maintain backward compatibility: Lemoniscate reads old formats and migrates automatically. No manual conversion required.
- Enable Mnemosyne message board sync by providing structured post data with IDs and timestamps (currently blocked by the flat text format).

## Capabilities

### New Capabilities
- `jsonl-message-board`: JSONL-based message board storage with structured posts (id, nick, login, body, timestamp). Covers storage format, append, read, migration from flat text, and Mnemosyne sync integration.
- `directory-threaded-news`: Directory-based threaded news storage with per-article JSON files and per-category metadata. Covers the directory layout, article CRUD, category/bundle management, migration from YAML, and Mnemosyne sync integration.

### Modified Capabilities
- `mnemosyne-sync`: Message board sync is currently deferred ("MessageBoard.txt stores posts as raw unstructured text"). With structured posts available, message board sync can be enabled via `POST /api/v1/sync/msgboard` using the `{mode: "full", posts: [...]}` format.

## Impact

- **Affected code**: `flat_news.c` / `flat_news.h` (replace or wrap with JSONL backend), `threaded_news_yaml.c` / `threaded_news_yaml.h` (replace or wrap with directory backend), `mnemosyne_sync.c` (enable msgboard sync), `config_loader.c` (no config changes — format is auto-detected).
- **Data files**: `MessageBoard.jsonl` (new), `News/` directory tree (new). Old files (`MessageBoard.txt`, `ThreadedNews.yaml`) are preserved as backups after migration.
- **Disk I/O**: News operations become per-file reads/writes instead of full-YAML rewrites. Message board appends are a single line write instead of a full file rewrite.
- **Dependencies**: None new. JSON is built with the existing `json_builder` from mnemosyne-support.
- **Compatibility**: Janus servers can share config directories with Lemoniscate (same news/board file format). Existing Mobius/Lemoniscate configs are auto-migrated on first startup.
