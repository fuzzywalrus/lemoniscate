# GUI Feature Parity Audit: Lemoniscate vs MobiusAdmin SwiftUI Reference

Audit date: 2026-03-19

Reference project: `/Users/greggant/Development/mobius-macOS-GUI`

Legend: [x] = implemented, [~] = partial/stub, [ ] = missing

---

## Right Panel Tabs

- [x] **Server** tab
- [x] **Logs** tab
- [x] **Accounts** tab
- [x] **Online** tab
- [x] **Files** tab
- [x] **News** tab

---

## Server Controls & Status

- [x] Start server
- [x] Stop server
- [x] Restart server
- [x] Reload config action
- [x] Setup wizard launch action
- [x] Server status indicator + status text
- [x] Port display in status/footer
- [x] Live stats badges (Connected, Peak, DL, UL)
- [x] Missing-binary warning

---

## Settings Panel

### General

- [x] Server name
- [x] Description
- [x] Banner file picker
- [x] File root picker
- [x] Config directory picker
- [x] Login agreement editor (native in-app editor)
- [x] Message board editor (native in-app editor)

### Network

- [x] Hotline port
- [x] Bonjour toggle

### Tracker Registration

- [x] Enable tracker registration toggle
- [x] Tracker list display
- [x] Tracker add/remove controls

### Files

- [x] Preserve resource forks toggle
- [x] Ignore-files pattern list
- [x] Ignore-files pattern add/remove controls

### News

- [x] News date format selector
- [x] News delimiter editor

### Security (HOPE)

- [x] Enable HOPE toggle
- [x] Legacy mode toggle
- [x] E2E prefix field

### TLS Encryption

- [x] Certificate file picker
- [x] Private key file picker
- [x] TLS port field
- [x] Status hint label

### Limits

- [x] Max downloads
- [x] Max downloads per client
- [x] Max connections per IP
- [x] Unlimited hint text

---

## Logs Experience

- [x] Log output display
- [x] Auto-scroll toggle
- [x] Clear logs action
- [x] Filter text field
- [x] stdout visibility toggle
- [x] stderr visibility toggle
- [x] Source-aware styling (stderr highlighting)

---

## Accounts Management

- [x] Accounts list view
- [x] Account create workflow (modal create sheet)
- [x] Account delete confirm flow
- [x] Account detail editor (login/name/file root + template)
- [x] Permission matrix editor
- [x] Permission templates (Guest/Admin/Custom)
- [x] Password reset workflow
- [x] Ban management (IPs/usernames/nicknames)

---

## Online Management

- [x] Online tab scaffold
- [x] Connected users table
- [x] Per-user moderation actions (ban IP)
- [x] Periodic refresh polling
- [x] Embedded live logs split view

---

## File Browser

- [x] File root browser tab
- [x] Breadcrumb/path display
- [x] Home/up navigation
- [x] Directory refresh action
- [x] Directory/file listing with metadata
- [x] Double-click directory navigation

---

## News Management

### Message Board

- [x] Inline message board editor
- [x] Unsaved-changes indicator
- [x] Save workflow

### Threaded News

- [x] Threaded news YAML support (category + article lifecycle)
- [x] Category list/sidebar
- [x] Add category
- [x] Delete category
- [x] Category article listing

---

## Setup Wizard / First Launch

- [x] First-launch wizard auto-presentation
- [x] Multi-step setup flow
- [x] Progress indicator and step navigation
- [x] Validation-gated next actions
- [x] Finish and "Start Server" completion actions
- [x] Wizard summary step

---

## About / Update UX

- [x] About panel with app version
- [x] Update check UI
- [x] Update download link
- [x] Project links (GUI/server/homepage)

---

## Priority TODO

### High Priority (core operator workflows)

1. ~~**Add missing right-panel tabs**: Accounts, Online, Files, News~~ DONE
2. ~~**Add account + ban management UI**~~ DONE (core CRUD + ban lists; advanced permission matrix/password reset pending)
3. ~~**Add online users UI with live refresh**~~ DONE
4. ~~**Add news/message board management UI**~~ DONE (message board + category lifecycle + article listing)

### Medium Priority (config parity + usability)

5. ~~**Add tracker list add/remove UI**~~ DONE
6. ~~**Add ignore-files pattern management UI**~~ DONE
7. ~~**Upgrade settings text editors to native in-app editors**~~ DONE
8. ~~**Add log filtering and source toggles (stdout/stderr)**~~ DONE

### Low Priority (polish)

9. ~~**Add setup wizard parity**~~ DONE
10. ~~**Add about/update-check panel parity**~~ DONE
11. ~~**Add footer stats badges (Connected/Peak/DL/UL)**~~ DONE
