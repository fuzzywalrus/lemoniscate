# Feature Parity Audit: Lemoniscate vs Mobius Go Reference

Audit date: 2026-03-20

Legend: [x] = implemented, [~] = partial/stub, [ ] = missing

---

## Transaction Handlers

### Chat & Messaging

- [x] **TranKeepAlive** (500) - Connection keepalive
- [x] **TranAgreed** (121) - Accept server agreement, set user info
- [x] **TranSetClientUserInfo** (304) - Change nickname/icon
- [x] **TranChatSend** (105) - Public and private chat messages, /me actions
- [x] **TranSendInstantMsg** (108) - Private messages, quoting, auto-reply, refuse PM check
- [x] **TranGetUserNameList** (300) - Connected user list
- [x] **TranGetClientInfoText** (303) - User info panel
- [x] **TranUserBroadcast** (355) - Admin broadcast to all users

### Private Chat Rooms

- [x] **TranInviteNewChat** (112) - Create private chat room + invite
- [x] **TranInviteToChat** (113) - Invite user to existing chat
- [x] **TranJoinChat** (115) - Join chat, get member list + subject
- [x] **TranLeaveChat** (116) - Leave chat, notify members
- [x] **TranRejectChatInvite** (114) - No-op (minimal server logic needed by design)
- [x] **TranSetChatSubject** (120) - Set chat room subject

### User Account Management

- [x] **TranGetUser** (352) - Get account details
- [x] **TranNewUser** (350) - Create account
- [x] **TranSetUser** (353) - Update account
- [x] **TranDeleteUser** (351) - Delete account
- [x] **TranListUsers** (348) - List all accounts
- [x] **TranUpdateUser** (349) - Batch account editor with nested subfield parsing (create/modify/rename/delete)
- [x] **TranDisconnectUser** (110) - Admin kick user

### Flat News / Message Board

- [x] **TranGetMsgs** (101) - Read message board
- [x] **TranOldPostNews** (103) - Post to message board

### Threaded News

- [x] **TranGetNewsCatNameList** (370) - List news categories
- [x] **TranGetNewsArtNameList** (371) - List articles in category
- [x] **TranGetNewsArtData** (400) - Read article content
- [x] **TranPostNewsArt** (410) - Post new article (with threading)
- [x] **TranDelNewsItem** (380) - Delete category/folder
- [x] **TranDelNewsArt** (411) - Delete article (with recursive option)
- [x] **TranNewNewsCat** (382) - Create news category
- [x] **TranNewNewsFldr** (381) - Create news folder

### File Operations

- [x] **TranGetFileNameList** (200) - List files in directory
- [x] **TranGetFileInfo** (206) - File metadata (type, creator, size, dates, comment)
- [x] **TranSetFileInfo** (207) - Update file comment/name
- [x] **TranDeleteFile** (204) - Delete file
- [x] **TranMoveFile** (208) - Move/rename file
- [x] **TranNewFolder** (205) - Create folder
- [x] **TranMakeFileAlias** (209) - Create symlink/alias

### File Transfers

- [x] **TranDownloadFile** (202) - File download with FILP wrapping
- [x] **TranUploadFile** (203) - File upload with FILP parsing, .incomplete staging, atomic rename
- [x] **TranDownloadFldr** (210) - Interactive multi-file protocol with recursive walk, FileHeaders, and per-file FILP
- [x] **TranUploadFldr** (213) - Interactive multi-file receive with path parsing, mkdir, and per-file FILP receive
- [x] **TranDownloadBanner** (212) - Banner image download
- [x] **File transfer resume** - Parses RFLT from FieldFileResumeData, seeks past offset in DATA fork

---

## Server Features

### Networking & Infrastructure

- [x] Rate limiting (per-IP token bucket)
- [x] Idle/away detection (300s timeout, 10s check interval)
- [x] Ban list (IP + username banning)
- [x] kqueue event loop (Tiger 10.4+ native)
- [x] Dual-port system (5500 main + 5501 transfers)
- [ ] TLS/SSL support (Mobius has configurable TLS ports)
- [ ] Redis integration (Mobius uses for bans, online tracking)

### Discovery & Registration

- [x] Tracker registration (UDP, periodic every 300s with live user count, config-driven)
  > **Note:** Packets send correctly but tested trackers (preterhuman, mainecyber, badmoon) don't list us. Trackers do a TCP connect-back probe with zero data — our 10s handshake timeout may be too slow. Needs further investigation with other trackers or tracker software.
- [x] Bonjour/mDNS (dns_sd.h integration, registers on local network)

> Note: Mobius has tracker registration but NO Bonjour. We have both, though tracker listing isn't working yet with public trackers.

### Login & Session

- [x] Handshake protocol
- [x] Login authentication (account lookup + password verify)
- [x] Server agreement display (with skip permission)
- [x] Server banner support (via transfer port)
- [x] User join/leave notifications (TranNotifyChangeUser / TranNotifyDeleteUser)
- [x] Admin flag detection (ACCESS_DISCON_USER)
- [x] Password hashing (salted SHA-1 via OpenSSL 0.9.7 on Tiger; bcrypt unavailable without bundling. Format: `sha1:<salt>:<hash>`. Falls back to plaintext comparison for migrating old accounts)

### User Notifications

- [x] User join broadcast (individual fields: ID, icon, flags, name)
- [x] User leave broadcast
- [x] User info change broadcast (name/icon change)
- [x] Away status broadcast (idle timeout + activity resume)

---

## Access Control (41 permission bits)

### Actively Checked (39 of 41)

- [x] ACCESS_DELETE_FILE (0)
- [x] ACCESS_UPLOAD_FILE (1)
- [x] ACCESS_DOWNLOAD_FILE (2)
- [x] ACCESS_RENAME_FILE (3)
- [x] ACCESS_MOVE_FILE (4)
- [x] ACCESS_CREATE_FOLDER (5)
- [x] ACCESS_READ_CHAT (9)
- [x] ACCESS_SEND_CHAT (10)
- [x] ACCESS_OPEN_CHAT (11)
- [x] ACCESS_CREATE_USER (14)
- [x] ACCESS_DELETE_USER (15)
- [x] ACCESS_OPEN_USER (16)
- [x] ACCESS_MODIFY_USER (17)
- [x] ACCESS_NEWS_READ_ART (20)
- [x] ACCESS_NEWS_POST_ART (21)
- [x] ACCESS_DISCON_USER (22)
- [x] ACCESS_CANNOT_BE_DISCON (23)
- [x] ACCESS_GET_CLIENT_INFO (24)
- [x] ACCESS_ANY_NAME (26)
- [x] ACCESS_NO_AGREEMENT (27)
- [x] ACCESS_SET_FILE_COMMENT (28)
- [x] ACCESS_MAKE_ALIAS (31)
- [x] ACCESS_BROADCAST (32)
- [x] ACCESS_NEWS_DELETE_ART (33)
- [x] ACCESS_NEWS_CREATE_CAT (34)
- [x] ACCESS_NEWS_DELETE_CAT (35)
- [x] ACCESS_NEWS_CREATE_FLDR (36)
- [x] ACCESS_NEWS_DELETE_FLDR (37)
- [x] ACCESS_UPLOAD_FOLDER (38)
- [x] ACCESS_DOWNLOAD_FOLDER (39)
- [x] ACCESS_SEND_PRIV_MSG (40)

### Not Implemented (2 of 41) — unused in Mobius too

- [ ] ACCESS_CLOSE_CHAT (12) - protocol artifact, not implemented in any known server
- [ ] ACCESS_CHANGE_OWN_PASS (18) - no dedicated transaction exists in protocol

### Recently Added

- [x] ACCESS_DELETE_FOLDER (6) - delete handler checks file vs folder
- [x] ACCESS_RENAME_FOLDER (7) - set_file_info checks file vs folder for rename
- [x] ACCESS_SHOW_IN_LIST (13) - users without this bit are hidden from user list
- [x] ACCESS_UPLOAD_ANYWHERE (25) - uploads restricted to "Uploads" or "Drop Box" folders
- [x] ACCESS_SET_FOLDER_COMMENT (29) - set_file_info checks folder comment permission
- [x] ACCESS_VIEW_DROP_BOXES (30) - file listing blocks drop box access without permission

---

## Priority TODO

### High Priority (core functionality gaps)

1. ~~**File upload receive** - TranUploadFile is a stub; need to accept incoming file data on transfer port~~ DONE
2. ~~**File transfer resume** - Clients expect RFLT resume support for interrupted transfers~~ DONE
3. ~~**Tracker registration** - Required for public server visibility~~ DONE (code works, trackers not listing — investigate later)
4. ~~**Password hashing** - Plaintext passwords are a security risk; integrate bcrypt~~ DONE (salted SHA-1 via Tiger's OpenSSL)

### Medium Priority (completeness)

5. ~~**Folder download** - TranDownloadFldr needs folder archiving + multi-file transfer~~ DONE
6. ~~**Folder upload** - TranUploadFldr needs folder receive logic~~ DONE
7. ~~**TranUpdateUser** (349) - Batch account editor for v1.5+ clients~~ DONE
8. ~~**ACCESS_CANNOT_BE_DISCON** check~~ Already implemented
9. ~~**Missing folder permission checks**~~ Most already implemented (RENAME_FILE, CREATE_FOLDER, NEWS_CREATE/DELETE_CAT/FLDR). Remaining: DELETE_FOLDER, RENAME_FOLDER (need file-vs-folder distinction)

### Low Priority (polish)

10. **ACCESS_SHOW_IN_LIST** - Hide users from user list
11. **ACCESS_VIEW_DROP_BOXES** - Drop box folder visibility
12. **ACCESS_CHANGE_OWN_PASS** - Self-service password change
13. **TLS/SSL support** - Encrypted connections
14. **Encoding support** - Mobius has UTF-8/MacRoman config; we assume MacRoman
15. ~~**malloc error on banner download**~~ FIXED — transfer manager uses internal array, not heap; callers were incorrectly free()ing the pointer
