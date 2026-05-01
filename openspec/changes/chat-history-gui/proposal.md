# Chat History — GUI Configuration Surface

## Why

Chat History (the parent `chat-history` change) shipped with full server functionality and is already enabled on `hotline.semihosted.xyz` — the prune timer fires hourly, the wire protocol is implemented, and YAML/plist/CLI paths to the 9 chat-history config keys all work. The Cocoa admin GUI is the only configuration surface that doesn't yet expose them.

Task 2.5 in the parent change deferred this work with a one-line "needs new Cocoa widgets + preferences panel resize" — that's the gap this change closes. Without it, an operator who edits any setting via the GUI silently strips every `ChatHistory*` key from `config.yaml` on save (because the GUI re-serialises the config dictionary from its widget state, and there are no widgets bound to those keys).

The parent change deliberately scoped the GUI work as a separate PR to keep the wire-protocol/storage work reviewable. This is that separate PR.

**Reference**: parent change [openspec/changes/chat-history/](../chat-history/), task 2.5

## What Changes

### A new "Chat History" disclosure section in the left settings panel

Sits in the existing settings panel between the Mnemosyne and Encoding sections. Uses the established disclosure-section + `addCheckboxWithHelp` / `addRow` helpers — no panel-resize machinery needed. The "preferences panel resize" worry in the parent task pre-dated the disclosure-collapse pattern maturing; the section folds when not in use.

Layout (top to bottom, four logical groups):

1. **Master**: `Enable chat history` checkbox. When OFF, every other widget in the section is greyed out — same pattern as `_hopeCheckbox` gating the rest of the Security/HOPE row.
2. **Retention**: `Max messages` and `Max days` integer fields (0 = unlimited).
3. **Legacy compat**: `Broadcast recent messages on join` checkbox, `Recent count` field, `Log joins/leaves as system messages` checkbox.
4. **Encryption at rest**: Key-file path field + `Choose…` button + `Generate Key…` button. The Generate button is the one-click parallel to the existing `_generateTLSCertButton` — writes 32 random bytes to `<configdir>/chat_history.key`, sets restrictive permissions (0600), and populates the path field. Includes an inline ⚠ caption when the field is empty: "Without a key, message bodies are stored plaintext."
5. **Advanced (folded by default)**: Token-bucket capacity and refill rate. Hidden behind a small "Advanced ▶" disclosure within the section because most operators should not touch these.

### Master-toggle grey-out behaviour

Mirrors the HOPE master toggle: when `Enable chat history` is OFF, all retention/legacy/encryption/advanced widgets are visible-but-disabled (`setEnabled:NO`). The disabled state is computed in a single helper called from both initial widget creation and the toggle's action selector.

### Version bump

Bump from `0.1.7` → `0.1.8` to mark the release in which the GUI catches up to the already-shipped server feature. Touches `src/main.c` `printf` and `src/hotline/hope.c` `HOPE_APP_STRING`. (The PPC branch keeps its own version line — out of scope.)

### Operator docs note

Add a short paragraph to `docs/SERVER.md` (or wherever chat-history operator docs live) clarifying that:
- The chat-history per-connection token bucket and the per-IP connection rate limiter (which feeds auto-ban) are **disjoint counters**.
- A user who spams chat will hit the chat-history rate limit but will not contribute to auto-ban.
- A connection-flooding scanner will hit the per-IP rate limit and may be auto-banned but does not consume chat-history tokens.

## Impact

- **Affected specs**: new `chat-history-gui` capability spec capturing the GUI behaviour contract (master-toggle gating, key generation, persistence round-trip).
- **Affected code**: `src/gui/AppController.h`, `src/gui/AppController+LayoutAndTabs.inc`, `src/gui/AppController+LifecycleConfig.inc`, plus a new action handler (likely an `AppController+ChatHistoryActions.inc` to keep the section self-contained), and `src/main.c` + `src/hotline/hope.c` for the version bump.
- **No protocol or storage change.** This is purely an admin-UI catch-up plus a version-string bump.
- **Backwards compatible.** Operators upgrading to 0.1.8 see the same `config.yaml` they had before, but now their GUI shows the chat-history toggles and their saves will preserve the keys instead of stripping them.
