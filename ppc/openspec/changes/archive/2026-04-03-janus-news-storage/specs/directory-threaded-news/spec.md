## ADDED Requirements

### Requirement: Directory-based threaded news storage
The server SHALL store threaded news as a directory tree under `News/`, with each category as a subdirectory containing `_meta.json` and numbered article files (`NNNN.json`).

#### Scenario: Category metadata
- **WHEN** a category or bundle exists
- **THEN** its directory SHALL contain `_meta.json` with `name` (string), `kind` ("category" or "bundle"), `guid` (16-byte array), `add_sn` (integer), and `delete_sn` (integer)

#### Scenario: Article storage
- **WHEN** an article is posted
- **THEN** it SHALL be written as `NNNN.json` (zero-padded 4-digit ID) containing `id` (integer), `title` (string), `poster` (string), `date` (ISO 8601 timestamp), `parent_id` (integer), and `body` (string)

#### Scenario: Bundle nesting
- **WHEN** a news bundle (folder) contains subcategories
- **THEN** each subcategory SHALL be a subdirectory within the bundle directory, each with its own `_meta.json` and article files

#### Scenario: Article deletion
- **WHEN** an article is deleted
- **THEN** its JSON file SHALL be removed from the category directory

#### Scenario: Category creation
- **WHEN** a new category is created
- **THEN** a new directory SHALL be created with a `_meta.json` containing a randomly generated 16-byte GUID

#### Scenario: Category deletion
- **WHEN** a category is deleted
- **THEN** its entire directory (including all articles and subcategories) SHALL be removed

### Requirement: Migration from ThreadedNews.yaml
The server SHALL auto-migrate from `ThreadedNews.yaml` to the `News/` directory tree on startup when the directory does not exist but the YAML file does.

#### Scenario: Auto-migration on first startup
- **WHEN** the config directory contains `ThreadedNews.yaml` but no `News/` directory
- **THEN** the server SHALL convert all categories and articles to the directory format and rename the old file to `ThreadedNews.yaml.legacy`

#### Scenario: Category GUIDs preserved
- **WHEN** migrating from YAML
- **THEN** existing category GUIDs from the YAML file SHALL be preserved in `_meta.json`

#### Scenario: Article data preserved
- **WHEN** migrating from YAML
- **THEN** article IDs, titles, posters, dates, parent_ids, and bodies SHALL be faithfully converted to JSON files

#### Scenario: Hotline date conversion
- **WHEN** migrating articles with Hotline-format dates (8-byte Mac epoch)
- **THEN** dates SHALL be converted to ISO 8601 format in the JSON files

#### Scenario: Already migrated
- **WHEN** the `News/` directory already exists
- **THEN** the server SHALL use it directly without touching `ThreadedNews.yaml`

### Requirement: Wire protocol compatibility
The directory-based news SHALL work with all existing Hotline clients. The storage format is internal — clients interact via the standard Hotline threaded news transactions.

#### Scenario: Get categories
- **WHEN** a client requests the news category list
- **THEN** the server SHALL enumerate `News/` subdirectories and return them in the standard Hotline format (category type bytes, GUIDs, article counts)

#### Scenario: Get article list
- **WHEN** a client requests articles in a category
- **THEN** the server SHALL read article JSON files from the category directory and return them in standard Hotline NewsArtListData format

#### Scenario: Post article
- **WHEN** a client posts an article
- **THEN** the server SHALL write a new numbered JSON file in the category directory with an auto-incremented ID
