## 1. Pre-flight verification

- [x] 1.1 Confirm `git-filter-repo` is installed — version `a40bce548d2c`
- [x] 1.2 Confirm working tree is clean aside from known pending items — the spec's original pending items (RELEASE_NOTES, swap file) no longer exist; actual pending items were ~16 untracked build artifacts (DMGs, `build/`, `lemoniscate-linux-*`, test binaries)
- [x] 1.3 Resolve the working-tree pending items — extended `.gitignore` to cover `*.dmg`, `build/`, `lemoniscate-linux-*`, `test_chacha20poly1305/_chat_history/_hope_aead/_threaded_news`
- [x] 1.4 Run `git fetch origin` to ensure local refs are current with remote
- [x] 1.5 Verify no open GitHub PRs exist against `main` or `modern` — empty
- [x] 1.6 Verify GitHub branch protection rules on `main` permit force-push — main has no protection (HTTP 404), force-push allowed
- [x] 1.7 Inspect `test_threaded_news` on `origin/modern` — confirmed Mach-O 64-bit x86_64 executable (binary, purge in task 4.2)
- [x] 1.8 Manually review the orphan tag `v0.1.0` — points at 3723cdf "Error handling" (GUI fixes, 2026-03-19); only referenced in this change's own docs; safe to drop with `legacy/v0.1.0` backup

## 2. Backup refs (Phase 0)

- [x] 2.1 Create backup tag `ppc-pre-flip` → pre-flip HEAD 8c463b6
- [x] 2.2 Create backup branch `ppc-archive` → pre-flip HEAD
- [x] 2.3 Back up 8 tags under `legacy/` prefix
- [x] 2.4 Push backup tag `ppc-pre-flip` → origin
- [x] 2.5 Push backup branch `ppc-archive` → origin (tracking set)
- [x] 2.6 Push 8 `legacy/*` tags → origin
- [x] 2.7 Verified on origin: `ppc-pre-flip`, `ppc-archive`, `legacy/v0.1.0–v0.1.7`

## 3. Filter-repo PPC side (Phase 1a)

- [x] 3.1 Created scratch clone /tmp/scratch-ppc
- [x] 3.2 Checked out main (pre-flip HEAD 8c463b6)
- [x] 3.3 filter-repo: 93 commits parsed, HEAD rewritten to 0db251c
- [x] 3.4 Verified paths prefixed with `ppc/`
- [x] 3.5 Verified: no `ppc/mobius-hotline-server` adds in history
- [x] 3.6 Verified: v0.1.5 tag points at ppc/-prefixed commit c7f64c6

## 4. Filter-repo modern side (Phase 1b)

- [x] 4.1 Created scratch clone /tmp/scratch-modern (used absolute path after an initial cwd bug)
- [x] 4.2 filter-repo purged DMG + test_threaded_news; HEAD rewritten to 45ece6a
- [x] 4.3 Verified: no `Lemoniscate-0.1.5.dmg` adds in history
- [x] 4.4 Verified: modern root intact (Dockerfile, Makefile, src/, docs/, openspec/)
- [x] 4.5 N/A — test_threaded_news confirmed binary in task 1.7

## 5. Perform the flip (Phase 2)

- [x] 5.1 Added scratch-modern + scratch-ppc remotes
- [x] 5.2 Fetched from both scratches
- [x] 5.3 Reset main to scratch-modern/modern (HEAD 45ece6a)
- [x] 5.4 Merged scratch-ppc/main (unrelated histories) → merge commit 0abc1d5
- [x] 5.5 Verified: root has modern tree, `ppc/` has PPC tree
- [x] 5.6 Verified: both histories reachable in `git log --all`

## 6. Post-merge cleanup (Phase 3)

- [x] 6.1 Deleted duplicate `ppc/LICENSE` (identical to root)
- [x] 6.2 Minimal-stub rewrite of root `README.md` — single-repo layout description, link to ppc/README.md (full rewrite deferred per user preference)
- [x] 6.3 Deleted `ppc/.gitignore` — contents fully covered by root `.gitignore` (which already has test_chacha20poly1305, test_chat_history, test_hope_aead, test_threaded_news, build/, lemoniscate-linux-*, Lemoniscate.app/, *.dmg)
- [x] 6.4 Scanned `README.md` and `docs/` for `src/` refs: root README is clean. `docs/PPC_BACKPORT_CHAT_HISTORY.md` has `src/` refs that are semantically "modern side" in backport context — left as-is; can be clarified later
- [x] 6.5 Commit cleanup: 065a68f

## 7. Tag reconciliation (Phase 4)

- [x] 7.1 Created `ppc/v0.1.4–ppc/v0.1.7` at scratch-ppc's rewritten SHAs (5b34134, c7f64c6, d2cfbae, cf9a7cb), deleted originals
- [x] 7.2 Verified: `v0.1.1`, `v0.1.2`, `v0.1.3` stay at original SHAs (9f0a52f, 345870a, 6d4263b) — these are un-prefixed modern-side ancestors and are reachable from new main
- [x] 7.3 N/A — ancestor tags already at correct un-prefixed commits; no re-point needed
- [x] 7.4 Deleted orphan `v0.1.0` tag
- [x] 7.5 Final local tag set: v0.1.1–v0.1.3, ppc/v0.1.4–ppc/v0.1.7, 8 legacy/*, ppc-pre-flip

## 8. Local validation before publish

- [x] 8.1 Verified: `ppc/src/main.c` traces through ppc/-prefixed history (cf9a7cb → d2cfbae → c9b1e31 → …)
- [x] 8.2 Verified: `src/` traces through modern history (aeb5386 → 8cba467 → 0886000 → …)
- [~] 8.3 Binaries STILL in pack locally (DMG 3.2MB, test_threaded_news 285KB, mobius-hotline-server ×4) — kept alive by `legacy/*` and `ppc-pre-flip` backup refs BY DESIGN. Objects become unreachable on origin once legacy tags are eventually pruned (out of scope for this change)
- [x] 8.4 Real repo `.git` = 47MB (holds both pre-flip and post-flip objects); scratch-ppc post-filter = 18MB, scratch-modern post-filter = 16MB — shows 5.7MB of pre-flip cruft will disappear from fresh clones after publish
- [x] 8.5 `openspec validate --all` in `ppc/` passes for flip-main-to-modern, port-modern-post-014, bootstrap-specs, gui-mnemosyne-encoding. Root `openspec/` has pre-existing validation failures unrelated to flip (janus-*, tracker-v3, voice-chat-*, etc.)

## 9. Publish (Phase 5 — point of no return)

- [x] 9.1 Force-pushed main: 60c52cb → 065a68f
- [x] 9.2 Pushed new `ppc/v0.1.4–v0.1.7` tags; deleted stale v0.1.0 and v0.1.4–v0.1.7 from origin (spec's `--tags` command doesn't delete, explicit deletes required)
- [x] 9.3 Created `archive/pre-flip-modern` on origin at 326cb2d (pre-flip modern HEAD)
- [x] 9.4 Deleted origin/modern
- [x] 9.5 Origin branches: main, archive/pre-flip-modern, ppc-archive. Tags: legacy/v0.1.0–v0.1.7, ppc-pre-flip, ppc/v0.1.4–v0.1.7, v0.1.1–v0.1.3. Visual check on GitHub pending user confirmation.
- [x] 9.6 Removed /tmp/scratch-ppc and /tmp/scratch-modern
- [x] 9.7 Removed scratch-modern and scratch-ppc remotes

## 10. Follow-ups (non-blocking)

- [x] 10.1 Updated `~/.claude/skills/powerpc-development/SKILL.md` (lines 71, 78): project path now `/Users/me/Public/lemoniscate-ppc/ppc`; added note about 2026-04-17 monorepo flip
- [x] 10.2 Built successfully on PowerMac G4 from `/Users/me/Public/lemoniscate-ppc/ppc` — libhotline.a and lemoniscate server both linked. Only pre-existing warnings (unused `proto` in tls_sectransport, pragma clang diagnostics in client.m, unused generate_sync_id/push_dir/pop_dir in mnemosyne_sync) — flip introduced no new build issues
- [ ] 10.3 (Optional) Rename the G4's SMB share top-level directory from `lemoniscate-ppc` to `lemoniscate/` so the path reflects the combined contents
- [ ] 10.4 Announce in any project-adjacent channels (Discord, README banner, release notes) that the branch flip occurred and point to this change as the record
