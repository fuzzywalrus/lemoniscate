## ADDED Requirements

### Requirement: Flat text message board parser for Mnemosyne sync
The server SHALL parse `MessageBoard.txt` into structured posts at Mnemosyne sync time. Each post SHALL contain `id` (auto-generated integer), `nick` (extracted from header), `body` (post text), and `timestamp` (extracted from header or file modification time).

#### Scenario: Parse post with standard header
- **WHEN** a flat text post has the format `"nick (date)\r\rbody"`
- **THEN** the parser SHALL extract the nick, date, and body as separate fields

#### Scenario: Parse post without parseable header
- **WHEN** a flat text post has no recognizable `"nick (date)"` header
- **THEN** the parser SHALL set nick to "unknown", timestamp to the file modification time, and use the entire text as body

#### Scenario: Split posts by delimiter
- **WHEN** parsing `MessageBoard.txt`
- **THEN** posts SHALL be split on the delimiter `"\r_____________________________________________\r"`

#### Scenario: Auto-generate post IDs
- **WHEN** parsing flat text posts
- **THEN** each post SHALL be assigned a sequential integer ID starting from 1

### Requirement: Janus JSONL message board reader
The server SHALL read `MessageBoard.jsonl` when it exists alongside `MessageBoard.txt`. Each line SHALL be parsed as `{id, nick, login, body, ts}`.

#### Scenario: JSONL preferred over flat text
- **WHEN** both `MessageBoard.jsonl` and `MessageBoard.txt` exist
- **THEN** the sync SHALL use JSONL as the source (already structured)

#### Scenario: JSONL not present
- **WHEN** only `MessageBoard.txt` exists
- **THEN** the sync SHALL fall back to flat text parsing

### Requirement: Janus directory news reader
The server SHALL read the `News/` directory tree when it exists for Mnemosyne sync. Categories SHALL be read from subdirectories with `_meta.json`, articles from numbered `NNNN.json` files.

#### Scenario: News directory preferred over YAML for sync
- **WHEN** both `News/` directory and `ThreadedNews.yaml` exist
- **THEN** the Mnemosyne news sync SHALL use the directory tree as the source

#### Scenario: No News directory
- **WHEN** only `ThreadedNews.yaml` exists
- **THEN** the Mnemosyne news sync SHALL use the in-memory YAML data (existing behavior)

### Requirement: Client backends unaffected
The sync content readers SHALL NOT affect client-facing operations. The Hotline wire protocol SHALL continue to use `MessageBoard.txt` via `flat_news.c` and `ThreadedNews.yaml` via `threaded_news_yaml.c`.

#### Scenario: Client reads message board
- **WHEN** a Hotline client requests the message board
- **THEN** the server SHALL return the raw flat text from `MessageBoard.txt` (unchanged behavior)

#### Scenario: Client reads threaded news
- **WHEN** a Hotline client requests news categories or articles
- **THEN** the server SHALL use the in-memory YAML data (unchanged behavior)
