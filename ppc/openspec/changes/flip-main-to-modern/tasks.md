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
- [ ] 6.5 Commit cleanup as a single commit

## 7. Tag reconciliation (Phase 4)

- [ ] 7.1 For each PPC-line tag, rename: `git tag ppc/v0.1.4 v0.1.4 && git tag -d v0.1.4`; repeat for v0.1.5, v0.1.6, v0.1.7
- [ ] 7.2 Verify ancestor tags v0.1.1, v0.1.2, v0.1.3 still point at the correct un-prefixed modern-side commits (`git show v0.1.1 --stat` should show paths without `ppc/` prefix)
- [ ] 7.3 If any ancestor tag points at a `ppc/`-prefixed commit, re-point it: `git tag -f v0.1.1 <correct-sha>` using the equivalent modern-side commit
- [ ] 7.4 Drop orphan tag: `git tag -d v0.1.0`
- [ ] 7.5 Verify final tag set: `git tag -l` should show v0.1.1, v0.1.2, v0.1.3, ppc/v0.1.4, ppc/v0.1.5, ppc/v0.1.6, ppc/v0.1.7, and 8 legacy/* tags

## 8. Local validation before publish

- [ ] 8.1 Verify `git log -- ppc/src/main.c` traces through history with `ppc/`-prefixed paths on all commits
- [ ] 8.2 Verify `git log -- src/` (modern) traces back to modern's original commits
- [ ] 8.3 Verify no binary artifacts exist in pack: `git rev-list --objects --all | git cat-file --batch-check='%(objecttype) %(objectname) %(objectsize) %(rest)' | awk '/^blob/{print $3, $4}' | sort -rn | head -10` — no `mobius-hotline-server`, `Lemoniscate-0.1.5.dmg`, or suspicious binaries in top entries
- [ ] 8.4 Verify repo size shrank: compare `.git` size pre-flip (from `ppc-archive`) and post-flip
- [ ] 8.5 Run `openspec validate` to ensure the openspec directory structure survived the merge

## 9. Publish (Phase 5 — point of no return)

- [ ] 9.1 Force-push main: `git push --force-with-lease origin main`
- [ ] 9.2 Force-push tags: `git push --force-with-lease --tags origin`
- [ ] 9.3 Create archive branch on origin from the pre-flip modern ref: `git push origin refs/remotes/origin/modern:refs/heads/archive/pre-flip-modern`
- [ ] 9.4 Delete origin/modern: `git push origin --delete modern`
- [ ] 9.5 Visual verification on GitHub: default branch shows modern tree at root, PPC under `ppc/`, branch list shows `main` + `archive/pre-flip-modern` + `ppc-archive`, tag list shows renamed + legacy tags
- [ ] 9.6 Clean up local scratch clones: `rm -rf /tmp/scratch-ppc /tmp/scratch-modern`
- [ ] 9.7 Clean up scratch remotes: `git remote remove scratch-ppc && git remote remove scratch-modern`

## 10. Follow-ups (non-blocking)

- [ ] 10.1 Update `~/.claude/skills/powerpc-development/SKILL.md`: change project path from `/Users/me/Public/lemoniscate-ppc` to `/Users/me/Public/lemoniscate-ppc/ppc` in all build examples
- [ ] 10.2 Test the PPC build end-to-end from the G4 via the updated skill to confirm nothing broke
- [ ] 10.3 (Optional) Rename the G4's SMB share top-level directory from `lemoniscate-ppc` to `lemoniscate/` so the path reflects the combined contents
- [ ] 10.4 Announce in any project-adjacent channels (Discord, README banner, release notes) that the branch flip occurred and point to this change as the record
