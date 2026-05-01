# Design — Chat History GUI

This change is small and almost entirely "extend the existing settings-panel pattern." Four design decisions are worth recording so reviewers and future-you don't have to reverse-engineer them.

## 1. One-click key generation

**Decision**: Add a `Generate Key…` button next to `Choose…` for the `ChatHistoryEncryptionKey` path field. Pattern mirrors the existing `_generateTLSCertButton` ([src/gui/AppController+LayoutAndTabs.inc:792](../../../src/gui/AppController+LayoutAndTabs.inc#L792)) which generates a self-signed TLS cert via a modal/sheet. The chat-history key generator is even simpler: 32 random bytes from `SecRandomCopyBytes` written to `<configdir>/chat_history.key` with `0600` permissions, then the path field is populated and the field's change-action fires (so the new path lands in the next save).

**Why**: design.md for the parent change is explicit that the encryption key is "32 bytes, raw." Operators have no native way to generate that — `dd if=/dev/urandom bs=32 count=1` works but is hostile UX, and no admin should be hand-typing 32 random bytes. Without a one-click path the encryption-at-rest feature is effectively dead-on-arrival in the GUI.

**Pitfall**: if a key file already exists at the target path, the button must refuse to overwrite (alert + prompt to choose a different filename) — overwriting silently makes every existing message body unreadable.

## 2. Master-toggle grey-out

**Decision**: When `Enable chat history` is OFF, all sibling widgets in the section are `setEnabled:NO`. State is computed by a single helper:

```objc
- (void)updateChatHistoryWidgetEnablement
{
    BOOL on = ([_chatHistoryEnabledCheckbox state] == NSControlStateValueOn);
    [_chatHistoryMaxMsgsField setEnabled:on];
    [_chatHistoryMaxDaysField setEnabled:on];
    /* …all other chat-history widgets… */
}
```

Called from (a) `loadSettings` after widget population, and (b) the master checkbox's action selector.

**Why**: matches the established HOPE pattern (the HOPE block disables its sub-widgets when `_hopeCheckbox` is off). Operator can see the available knobs without enabling the feature, which is more discoverable than hiding them entirely.

**Pitfall**: don't forget the Generate-Key button — it must also disable when chat-history is off, otherwise an operator can generate a key for a feature they haven't enabled and end up with a stray file.

## 3. Rate-limit knobs behind an "Advanced" disclosure

**Decision**: `ChatHistoryRateCapacity` (default 20 tokens) and `ChatHistoryRateRefillPerSec` (default 10 tokens/sec) live behind a small disclosure-within-the-section labelled "Advanced ▶". Folded by default. Implementation can reuse the existing `makeDisclosureHeader` machinery used at the top-level disclosure sections — that helper does not require being a top-level section.

**Why**: defaults are tuned for normal interactive chatting and are not a knob most operators should touch. Surfacing them at the same level as `Max messages` invites well-meaning operators to "tune" them and break their server's rate-limit semantics. Hiding them entirely makes them undiscoverable. Disclosure-fold is the right middle ground.

**Pitfall**: if the disclosure machinery turns out to require a top-level section (it shouldn't — but worth verifying when implementing), fall back to two read-only `NSTextField` labels showing the current values, with a sentence directing operators to edit YAML directly. Better than hiding the knobs from view entirely.

## 4. Operator docs: rate limiters are disjoint

**Decision**: Add a short paragraph to `docs/SERVER.md` clarifying that the chat-history per-connection token bucket (this change) and the per-IP connection rate limiter (which feeds the auto-ban added in commit `9755822`) are **disjoint counters**.

**Why**: the two limits look superficially similar (both token buckets, both per-something). Without explicit docs, operators tuning one will assume they understand the other. The actual semantics:
- **Per-IP connection rate limit** (HL_PER_IP_RATE_INTERVAL = 2s): one new TCP connection per IP per N seconds. Hits feed `HL_AUTOBAN_VIOLATION_THRESHOLD` → auto-ban → persistent banlist → kernel drop via fail2ban.
- **Per-connection chat send rate limit** (ChatHistoryRateCapacity / RefillPerSec): one chat message per connection per N tokens. Hits return a polite "rate-limited" error to that connection and do not affect auto-ban.

A user spamming chat from one TCP session burns chat tokens but never trips auto-ban. A scanner opening 50 connections trips auto-ban but never burns a chat token. Both behaviours are intentional.

**Pitfall**: don't be tempted to "unify" the two counters in code — they're tracking fundamentally different abuse vectors and should stay independent.

## Out of scope

- Live preview of recent chat history inside the GUI — admins use clients for that, the GUI is for operations not for chat reading.
- Per-channel rate-limit overrides — the existing config is global only; channel-scoped tuning is its own design conversation.
- Migration UI for moving from plaintext to encrypted storage — design.md for the parent change defers this to operator scripting.
- Touching the PPC GUI — PPC has its own AppController fork; bump and feature-port land in the PPC backport change, not here.
