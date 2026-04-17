## 1. Initialize OpenSpec Specs Directory

- [ ] 1.1 Create `openspec/specs/` directory structure with subdirectories for all 8 capabilities
- [ ] 1.2 Verify openspec schema configuration supports the spec-driven workflow

## 2. Archive Wire Protocol Spec

- [ ] 2.1 Archive `wire-protocol/spec.md` — handshake, transaction framing, field TLV encoding, big-endian integers, handler dispatch, version 190, text encoding
- [ ] 2.2 Cross-reference wire format constants against `include/hotline/transaction.h`, `field.h`, and `handshake.h` for accuracy

## 3. Archive Authentication Spec

- [ ] 3.1 Archive `authentication/spec.md` — plaintext login, guest access, HOPE secure login, TLS encryption, login reply metadata, agreement display
- [ ] 3.2 Cross-reference HOPE field constants against `include/hotline/hope.h` and password obfuscation against `include/hotline/user.h`

## 4. Archive Chat & Messaging Spec

- [ ] 4.1 Archive `chat-messaging/spec.md` — public chat, private chat rooms, instant messages, auto-reply, /me actions, admin broadcast
- [ ] 4.2 Cross-reference chat manager interface against `include/hotline/chat.h`

## 5. Archive File Transfer Spec

- [ ] 5.1 Archive `file-transfer/spec.md` — browsing, download/upload, resume, folder transfers, FILP format, type/creator codes, aliases, banner download
- [ ] 5.2 Cross-reference transfer types and FILP format against `include/hotline/file_transfer.h` and `include/hotline/file_types.h`

## 6. Archive User Management Spec

- [ ] 6.1 Archive `user-management/spec.md` — account CRUD, 41-bit permissions, user list, user info, flags, kick/disconnect, account list, change notifications
- [ ] 6.2 Cross-reference user wire format and flag bits against `include/hotline/user.h` and `include/hotline/access.h`

## 7. Archive News Board Spec

- [ ] 7.1 Archive `news-board/spec.md` — flat message board, threaded news categories/articles, post/reply, delete articles/categories
- [ ] 7.2 Cross-reference persistence format against `include/mobius/flat_news.h` and `include/mobius/threaded_news_yaml.h`

## 8. Archive Networking Spec

- [ ] 8.1 Archive `networking/spec.md` — kqueue event loop, tracker UDP heartbeat, Bonjour/mDNS, rate limiting, idle disconnect, dual-port architecture, IP ban enforcement
- [ ] 8.2 Cross-reference server networking against `include/hotline/server.h`, `tracker.h`, and `bonjour.h`

## 9. Archive Server Config Spec

- [ ] 9.1 Archive `server-config/spec.md` — YAML/plist config loading, --init scaffolding, CLI flag overrides, SIGHUP reload, agreement serving, ban list persistence, graceful shutdown
- [ ] 9.2 Cross-reference config fields against `include/hotline/config.h` and `include/mobius/config_loader.h`

## 10. Validation

- [ ] 10.1 Run `openspec status` to confirm all specs are archived and the change is complete
- [ ] 10.2 Verify no spec references non-existent source files or headers
