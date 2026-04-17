## ADDED Requirements

### Requirement: Flat message board read and write
The server SHALL maintain a flat (non-threaded) message board stored as a text file. Users can read the full board and post new messages that are prepended to the existing content.

#### Scenario: Read message board
- **WHEN** a client sends a get-message-board transaction
- **THEN** the server returns the full text content of the message board file

#### Scenario: Post to message board
- **WHEN** a client with message-board-post permission sends a post transaction with message text
- **THEN** the server prepends the message (with sender name and timestamp) to the message board file

#### Scenario: Post without permission
- **WHEN** a client without message-board-post permission attempts to post
- **THEN** the server replies with a permission-denied error

### Requirement: Threaded news with categories and articles
The server SHALL support threaded news organized into categories. Each category contains articles with subject, sender, date, and body. Categories and articles are persisted in YAML format.

#### Scenario: List news categories
- **WHEN** a client sends a get-news-categories transaction
- **THEN** the server returns the tree of news categories

#### Scenario: List articles in category
- **WHEN** a client sends a get-news-article-list transaction for a valid category
- **THEN** the server returns a list of article headers (subject, sender, date, parent article)

#### Scenario: Read article
- **WHEN** a client sends a get-news-article transaction with a category and article ID
- **THEN** the server returns the full article body

### Requirement: Post and reply to threaded news articles
The server SHALL allow users to post new articles and reply to existing articles within a news category. Replies are linked to their parent article to form threads.

#### Scenario: Post new article
- **WHEN** a client with news-post permission sends a post-article transaction with subject and body
- **THEN** the server creates the article in the specified category and persists it

#### Scenario: Reply to existing article
- **WHEN** a client sends a post-article transaction with a parent article ID
- **THEN** the server creates a reply article linked to the parent

#### Scenario: Post without news permission
- **WHEN** a client without news-post permission attempts to post an article
- **THEN** the server replies with a permission-denied error

### Requirement: Delete news articles and categories
The server SHALL allow privileged users to delete news articles and categories.

#### Scenario: Delete article
- **WHEN** a client with news-delete permission sends a delete-article transaction
- **THEN** the server removes the article from the category

#### Scenario: Delete category
- **WHEN** a client with news-category-delete permission sends a delete-category transaction
- **THEN** the server removes the category and all its articles

### Requirement: Create news categories
The server SHALL allow privileged users to create new news categories for organizing articles.

#### Scenario: Create category
- **WHEN** a client with news-category-create permission sends a create-category transaction with a name
- **THEN** the server creates the category and it becomes available for posting
