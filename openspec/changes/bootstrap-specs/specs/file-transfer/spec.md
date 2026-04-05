## ADDED Requirements

### Requirement: File browsing returns directory listings
The server SHALL return file listings for requested directories within the file root. Each entry includes the file name, size, type code, creator code, and modification date.

#### Scenario: Browse root directory
- **WHEN** a client sends a get-file-list transaction with no path
- **THEN** the server returns a listing of files and folders in the configured file root

#### Scenario: Browse subdirectory
- **WHEN** a client sends a get-file-list transaction with a valid subdirectory path
- **THEN** the server returns the listing for that subdirectory

#### Scenario: Browse non-existent directory
- **WHEN** a client sends a get-file-list transaction with a path that does not exist
- **THEN** the server replies with an error

#### Scenario: Path traversal prevention
- **WHEN** a client sends a file path containing ".." or other traversal sequences
- **THEN** the server rejects the request and does not access files outside the file root

### Requirement: File download via transfer port
The server SHALL support file downloads. The client requests a download, receives a transfer reference number, then connects to the transfer port (default 5501) with that reference to receive the file data.

#### Scenario: Single file download
- **WHEN** a client sends a download-file transaction for an existing file
- **THEN** the server creates a transfer record, returns a reference number and file size, and serves the file data when the client connects to the transfer port with that reference

#### Scenario: Download non-existent file
- **WHEN** a client requests a download for a file that does not exist
- **THEN** the server replies with an error

### Requirement: File upload via transfer port
The server SHALL accept file uploads from clients with upload permission. The client requests an upload, receives a transfer reference, then connects to the transfer port to send the file data.

#### Scenario: Single file upload
- **WHEN** a client with upload permission sends an upload-file transaction
- **THEN** the server creates a transfer record, returns a reference number, and accepts the file data on the transfer port

#### Scenario: Upload without permission
- **WHEN** a client without upload permission sends an upload-file transaction
- **THEN** the server replies with a permission-denied error

### Requirement: Resumable file transfers
The server SHALL support resuming interrupted file transfers. The client includes resume data (byte offset) in the download or upload request. The server resumes from the specified offset.

#### Scenario: Resume interrupted download
- **WHEN** a client sends a download request with resume data specifying a byte offset
- **THEN** the server begins sending file data from the specified offset

### Requirement: Folder download and upload
The server SHALL support downloading and uploading entire folders. Folder transfers send/receive files sequentially with per-item negotiation (send-file, resume-file, next-file actions).

#### Scenario: Folder download
- **WHEN** a client sends a download-folder transaction for an existing directory
- **THEN** the server sends folder contents item-by-item, using the folder download action protocol (send-file=1, resume-file=2, next-file=3)

#### Scenario: Folder upload
- **WHEN** a client with folder upload permission sends an upload-folder transaction
- **THEN** the server accepts the folder contents item-by-item on the transfer port

### Requirement: FILP format with fork preservation
File transfers SHALL use the FILP (File Information and Lookup Protocol) format, which preserves both the data fork and resource fork of files. This maintains compatibility with classic Mac file structures.

#### Scenario: Download file with resource fork
- **WHEN** a client downloads a file that has a resource fork
- **THEN** the transfer includes both the data fork and resource fork in FILP format

### Requirement: File type and creator code mapping
The server SHALL map file extensions to Macintosh type and creator codes for over 70 file types, ensuring correct icon display in Hotline clients.

#### Scenario: Known file extension
- **WHEN** a file listing includes a file with a recognized extension (e.g., .jpg, .mp3, .txt)
- **THEN** the listing entry contains the correct 4-byte type and creator codes

#### Scenario: Unknown file extension
- **WHEN** a file listing includes a file with an unrecognized extension
- **THEN** the listing entry uses default type/creator codes

### Requirement: File alias resolution
The server SHALL resolve macOS file aliases when encountered during directory browsing or file operations.

#### Scenario: Browse directory containing aliases
- **WHEN** a directory listing includes a macOS alias file
- **THEN** the server resolves the alias and lists the target file/folder

### Requirement: Banner download
The server SHALL support banner image downloads as a special transfer type (type 4). The banner is served when requested after login.

#### Scenario: Banner download
- **WHEN** a client requests a banner download and the server has a banner configured
- **THEN** the server serves the banner image data via the transfer port
