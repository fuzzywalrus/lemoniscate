# Lemoniscate Server Features

**Version 0.1.1** — A Hotline server for Mac OS X Tiger (PowerPC)

---

## Chat & Messaging

- Public chat room with all connected users
- Private chat rooms — create, invite users, set topics
- Instant messages (private messages between users)
- Auto-reply when a user is away or idle
- /me action messages
- Admin broadcast messages to all users

## User Management

- User accounts with login and password (salted SHA-1 hashed)
- Guest access (no login required)
- 41 individual permission bits per account
- Admin account editor — create, modify, rename, and delete accounts
- Batch account editing (v1.5+ multi-user editor)
- Kick/disconnect users (with optional message)
- Protected accounts that can't be disconnected

## File Sharing

- Browse server files and folders
- Download files with progress tracking
- Upload files to the server
- Download entire folders (recursive)
- Upload entire folders (recursive)
- Resume interrupted downloads
- File info — type, creator, size, dates, comments
- Rename, move, and delete files and folders
- Create new folders
- Create file aliases (symlinks)
- 70+ file type mappings for correct Mac type/creator codes

## News & Message Board

- Flat message board (classic Hotline "News")
- Threaded news with categories and articles
- Create and delete news categories
- Post, read, and delete articles with threading
- News data persists across server restarts

## Server Administration

- GUI admin application (Cocoa, native Tiger look)
- Setup wizard for first-time configuration
- macOS plist configuration (native format)
- YAML fallback for compatibility
- Server agreement displayed on login
- Server banner image
- Default banner bundled with the app
- Start, stop, and restart from the GUI
- Live server logs in the admin interface
- Log file saved to Application Support

## Networking

- Hotline protocol on port 5500 (configurable)
- File transfers on port 5501
- Bonjour/mDNS for local network discovery
- Tracker registration (UDP, periodic with live user count)
- Idle/away detection (5 minutes, auto-broadcasts status)
- Per-IP rate limiting
- Ban list (IP addresses and usernames)

## Access Control

Granular per-account permissions including:

- Download, upload, delete, rename, and move files
- Create folders and file aliases
- Read and send chat
- Create and manage private chat rooms
- Read, post, and delete news articles
- Create and delete news categories
- View and modify user accounts
- Disconnect other users
- Send broadcast messages
- Upload location restrictions (Uploads/Drop Box only)
- Drop box folder visibility control

## Compatibility

- Runs on Mac OS X 10.4 Tiger (PowerPC)
- Works with Hotline Navigator, the mierau Swift client, and classic Hotline clients
- FILP file transfer format with INFO and DATA forks
- Hotline 1.8+ protocol (version 190)
- CLI server binary for headless operation
- Mobius-compatible account and news file formats
