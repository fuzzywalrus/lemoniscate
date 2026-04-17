## 1. Pre-flight verification

- [ ] 1.1 Confirm `git-filter-repo` is installed (`git filter-repo --version`); if missing, install via `brew install git-filter-repo`
- [ ] 1.2 Confirm working tree is clean aside from known pending items (`RELEASE_NOTES_0.1.5.md` staged delete, `..openspec.yaml.TsYk9xjbpE` untracked stray)
- [ ] 1.3 Resolve the working-tree pending items — commit the RELEASE_NOTES deletion or restore the file; delete the swap file
- [ ] 1.4 Run `git fetch origin` to ensure local refs are current with remote
- [ ] 1.5 Verify no open GitHub PRs exist against `main` or `modern` (`gh pr list --state open`) — expect empty
- [ ] 1.6 Verify GitHub branch protection rules on `main` permit force-push (repo settings); document any rules that need temporary disabling
- [ ] 1.7 Inspect `test_threaded_news` on `origin/modern` — run `git show origin/modern:test_threaded_news | file -` to confirm it's a compiled binary and not source
- [ ] 1.8 Manually review the orphan tag `v0.1.0` (`git show v0.1.0`) and grep project docs for any mention — confirm it's safe to drop

## 2. Backup refs (Phase 0)

- [ ] 2.1 Create backup tag: `git tag ppc-pre-flip HEAD`
- [ ] 2.2 Create backup branch: `git branch ppc-archive HEAD`
- [ ] 2.3 Back up all 8 existing tags under `legacy/` prefix: `for t in v0.1.0 v0.1.1 v0.1.2 v0.1.3 v0.1.4 v0.1.5 v0.1.6 v0.1.7; do git tag legacy/$t $t; done`
- [ ] 2.4 Push backup tag: `git push origin ppc-pre-flip`
- [ ] 2.5 Push backup branch: `git push -u origin ppc-archive`
- [ ] 2.6 Push legacy tags: `git push origin legacy/v0.1.0 legacy/v0.1.1 legacy/v0.1.2 legacy/v0.1.3 legacy/v0.1.4 legacy/v0.1.5 legacy/v0.1.6 legacy/v0.1.7`
- [ ] 2.7 Verify all backup refs visible on origin via `git ls-remote origin | grep -E 'ppc-(pre-flip|archive)|legacy/'`

## 3. Filter-repo PPC side (Phase 1a)

- [ ] 3.1 Create scratch clone: `git clone --no-local . /tmp/scratch-ppc` (non-local to avoid hardlinks)
- [ ] 3.2 In scratch-ppc, checkout main: `cd /tmp/scratch-ppc && git checkout main`
- [ ] 3.3 Run filter-repo with subdirectory prefix + binary purge: `git filter-repo --to-subdirectory-filter ppc/ --path ppc/mobius-hotline-server --invert-paths --force`
- [ ] 3.4 Verify paths are prefixed: `git log --name-only -5` should show `ppc/…` paths on recent commits
- [ ] 3.5 Verify binary purge: `git log --all --diff-filter=A -- ppc/mobius-hotline-server` should be empty
- [ ] 3.6 Verify tags rewrote: `git tag -l v0.1.5` should exist and `git show v0.1.5 --stat` should show `ppc/…` paths

## 4. Filter-repo modern side (Phase 1b)

- [ ] 4.1 Create scratch clone: `git clone --no-local --branch modern . /tmp/scratch-modern`
- [ ] 4.2 Run filter-repo to purge DMG and (if binary per task 1.7) `test_threaded_news`: `cd /tmp/scratch-modern && git filter-repo --path Lemoniscate-0.1.5.dmg --invert-paths --path test_threaded_news --invert-paths --force`
- [ ] 4.3 Verify DMG purge: `git log --all --diff-filter=A -- Lemoniscate-0.1.5.dmg` should be empty
- [ ] 4.4 Verify modern tree at root is intact: `ls` should show `Dockerfile`, `Makefile`, `src/`, etc.
- [ ] 4.5 If `test_threaded_news` was source not binary, repeat task 4.2 without the `test_threaded_news` path

## 5. Perform the flip (Phase 2)

- [ ] 5.1 In real repo, add scratches as remotes: `git remote add scratch-modern /tmp/scratch-modern && git remote add scratch-ppc /tmp/scratch-ppc`
- [ ] 5.2 Fetch from both scratches: `git fetch scratch-modern && git fetch scratch-ppc`
- [ ] 5.3 Reset `main` to scratch-modern's tip: `git reset --hard scratch-modern/modern`
- [ ] 5.4 Merge scratch-ppc's main with unrelated histories: `git merge --allow-unrelated-histories scratch-ppc/main -m "Flip repo: modern at root, PPC under ppc/, histories joined"`
- [ ] 5.5 Verify the merge tree: `ls` shows modern at root, `ls ppc/` shows PPC tree
- [ ] 5.6 Verify both histories reachable: `git log --oneline --all | head -20` shows commits from both lines

## 6. Post-merge cleanup (Phase 3)

- [ ] 6.1 Delete duplicate LICENSE: `rm ppc/LICENSE`
- [ ] 6.2 Rewrite root `README.md` to document: repo layout overview, link to `ppc/README.md` for PPC build, modern build instructions, backport workflow, tag naming convention
- [ ] 6.3 Review `.gitignore` at root vs `ppc/.gitignore`; merge PPC-specific patterns into root where they apply globally; delete `ppc/.gitignore` if empty
- [ ] 6.4 Scan for any other path-dependent references that broke (grep for `src/` in docs/scripts that now should point to `ppc/src/`): `git grep -n '\bsrc/' README.md docs/`
- [ ] 6.5 Commit cleanup as a single commit: `git commit -m "Post-flip cleanup: dedupe LICENSE, rewrite README, consolidate .gitignore"`

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
