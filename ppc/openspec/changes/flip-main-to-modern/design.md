## Context

The lemoniscate repository holds two related codebases on long-lived branches:

- `main` — the PPC C port targeting Mac OS X 10.4 Tiger / 10.5 Leopard on PowerPC. Tip: `1137737`.
- `origin/modern` — the modern Go/Docker codebase targeting Linux and modern macOS. Tip: `326cb2d`.

The branches diverged from `ea86c90` (tagged `v0.1.3`) on 2026-03-22 — 13 ahead / 28 behind at the time of writing. The merge base is young (~3.5 weeks). The project is solo-maintained; no external CI references, no GitHub Actions workflows, no open PRs, no submodules, no LFS, no committed `.env`.

The observed development flow is modern-first: new features land on `modern`, then get hand-backported to `main` as PPC-compatible C. Every backported feature creates two commits — one per branch — which inflates the ahead/behind counts beyond their semantic meaning. Reviewers coming to the GitHub project page see `main` (PPC, legacy) as the default and have to know to switch to `modern` to see current work.

Both branches have committed binary artifacts in history — total recoverable after purge ≈ 5.7 MB:

| Artifact | Branch | Copies | Size |
|---|---|---|---|
| `Lemoniscate-0.1.5.dmg` | modern | 1 | 3.3 MB |
| `mobius-hotline-server` | main | 7 historical | ~1.9 MB |
| `test_threaded_news` | modern (suspected) | 2 | ~570 KB |

Eight tags span the project: `v0.1.0` (orphan — points at commit `3723cdf` not reachable from any branch), `v0.1.1`–`v0.1.3` (on both branches, ancestor of divergence), `v0.1.4`–`v0.1.7` (only on `main`, PPC-line releases).

## Goals / Non-Goals

**Goals:**
- Single long-lived branch (`main`) with modern at root, PPC under `ppc/`.
- Full commit history of both codebases preserved in `main` after the flip.
- Recoverable via pre-flip backup refs if the migration goes sideways before pushing.
- PPC-line tags (`v0.1.4`–`v0.1.7`) renamed to `ppc/v0.1.x` to distinguish PPC-specific releases from future unified releases.
- Committed binaries purged from history to reduce clone size and enforce the convention going forward.
- The `powerpc-development` skill and the canonical Tiger build workflow continue to work against the new path (`ppc/` subdir).

**Non-Goals:**
- Unifying the two `openspec/` directories. Modern's openspec stays at `/openspec/`, PPC's moves to `/ppc/openspec/`. They track different codebases and should remain independent.
- A top-level build dispatcher (Makefile, justfile, etc.) that dispatches to both codebases. Builds are too different; users run commands directly in the codebase they care about.
- Changing release artifact policy (whether to commit them vs. use GitHub Releases). The purge is a one-time cleanup; future policy is a separate decision.
- Renaming the SMB share's top-level directory on the G4. That's a followup step, not a prerequisite for the migration.
- Preserving commit SHAs. Both branches get fully SHA-rewritten. External links to specific commit hashes will break; external links to tag names survive because GitHub resolves tag-to-SHA at render time.

## Decisions

### 1. `git filter-repo` over `git subtree add` for the history rewrite

**Chosen**: `git filter-repo --to-subdirectory-filter ppc/` on a scratch clone of `main`.

**Alternative considered**: `git subtree add --prefix=ppc/ <ppc-ref>` (no `--squash`). Would preserve original PPC SHAs, but commits in ppc's history touch un-prefixed paths (`src/foo.c`, not `ppc/src/foo.c`). `git blame ppc/src/foo.c` would trace back into the subtree merge and then show original paths, which is confusing during future archaeology.

**Rationale**: The user explicitly said they don't need stable SHAs — they want the historical record for archaeology, and archaeology is easier when paths match the tree. The rewrite cost is that commit hashes change; the reward is that `git log -- ppc/` and `git blame ppc/...` work naturally forever. The ancestor tags `v0.1.1`–`v0.1.3` point at commits that also exist (untouched) in modern's ancestry, so they survive on that side even though the PPC-side copies are rewritten.

### 2. Two scratch clones and two separate filter-repo passes, not one combined pass

**Chosen**: Scratch A clones `main` and runs `filter-repo --to-subdirectory-filter ppc/ --path ppc/mobius-hotline-server --invert-paths`. Scratch B clones `origin/modern` and runs `filter-repo --path Lemoniscate-0.1.5.dmg --invert-paths` (plus `test_threaded_news` if confirmed present). The real repo then resets `main` to scratch B's tip and merges scratch A's tip with `--allow-unrelated-histories`.

**Alternative considered**: Do a simple subtree flip first, then one combined `filter-repo` pass over the merged repo to prefix + purge. `git-filter-repo` does support multiple operations, but combining `--to-subdirectory-filter` with per-path inversions requires careful ordering and makes the "I'm only rewriting this branch's history" scope unclear. Two passes keeps each one's job obvious.

**Rationale**: Separate scratch clones isolate failure modes. If something goes wrong filtering PPC, modern is untouched. If the final merge conflicts, we re-run only the relevant scratch. The real repo is never the operand of a destructive history rewrite — it only receives fetches and performs a merge.

### 3. Tag handling: back up all under `legacy/`, rename PPC-line post-flip, drop the orphan

**Chosen**:
- Pre-flip: `git tag legacy/v0.1.0 v0.1.0; git tag legacy/v0.1.1 v0.1.1; … ; git push origin --tags` to backup all 8.
- Let `filter-repo` auto-rewrite tags on the scratch clones (default behavior — tag refs in the scratch get updated to point at the new rewritten commits).
- Fetch rewritten tags back from scratch into the real repo (they'll supersede the original refs locally).
- Post-merge: manually rename `v0.1.4`–`v0.1.7` to `ppc/v0.1.4`–`ppc/v0.1.7` (they tag PPC-specific releases). Leave `v0.1.1`–`v0.1.3` plain — they're ancestor tags predating divergence. Drop `v0.1.0` (orphan, not reachable).
- Push tags with `--force-with-lease`.

**Alternative considered**: Keep all tags pointing at their original (pre-rewrite) commits, relying on the backup branch `ppc-archive` to keep them reachable. Simpler, but creates a confusing split between "tags" (old hashes) and "main" (new hashes). Archaeology from a tag jumps into orphaned history that diverges from the live repo.

**Rationale**: Backup under `legacy/` is the safety net. Rewriting the live tags keeps the live repo internally consistent. The `ppc/` prefix on renamed tags makes it obvious at-a-glance which side of the flip a tag belongs to, which matters once future unified releases get `v0.2.x` tags.

### 4. Force-push scope: `main` + all tags + `origin/modern` rename

**Chosen**: Single coordinated push window:
1. `git push --force-with-lease origin main` — flips the repo.
2. `git push --force-with-lease --tags origin` — updates rewritten tags and pushes new `ppc/v0.1.x` and `legacy/*` refs.
3. `git push origin refs/remotes/origin/modern:refs/heads/archive/pre-flip-modern` — preserves `modern` under a read-only-intent name.
4. `git push origin --delete modern` — removes the original `modern` ref.
5. Verify GitHub UI shows new layout; if wrong, roll back immediately (see Migration Plan).

**Rationale**: `--force-with-lease` prevents clobbering if something unexpected is on the remote. `archive/pre-flip-modern` keeps `modern`'s content visible under a name that signals "don't land new work here" — distinct from just deleting the branch, which would lose the reference point.

### 5. Duplicate `LICENSE` at `/LICENSE` and `/ppc/LICENSE` — delete the PPC-side copy

**Chosen**: After the merge, `rm ppc/LICENSE` and commit.

**Rationale**: Both branches have identical LICENSE files (presumably). Root license governs the whole repo. Keeping a nested copy is redundant and creates a "which is canonical?" question that has no useful answer.

### 6. `README.md`: rewrite root, keep `ppc/README.md` as-is

**Chosen**: Root `README.md` is rewritten to explain the new layout — links to `ppc/README.md` for PPC build instructions, to modern's existing docs for modern build, and documents the monorepo conventions (where things live, how to backport, tag naming).

**Rationale**: Modern's README becomes the root README by virtue of the reset. It needs content touching both codebases. Rather than awkwardly merging two READMEs, rewrite it with explicit structure. PPC's README stays untouched under `ppc/` — it's still valid for readers focused on PPC.

### 7. `.gitignore` rules: merge at root

**Chosen**: Audit both branches' `.gitignore` files after the flip. Merge PPC-specific patterns into the root `.gitignore` so they apply to the whole repo. Delete `ppc/.gitignore` if it becomes empty after the merge.

**Rationale**: `.gitignore` rules compound hierarchically, so a `ppc/.gitignore` is fine, but most patterns (e.g., `*.o`, `.DS_Store`, `.app/`) apply globally. Root ignore is clearer and avoids duplication.

## Risks / Trade-offs

**[Risk] Filter-repo on a fresh clone of the wrong remote or wrong branch** → **Mitigation**: Scratches are made from specific refs (`origin/main` and `origin/modern`) with explicit checkout commands. Pre-flip the real repo's tip is tagged `ppc-pre-flip` so any confusion can be audited against a known anchor.

**[Risk] Ancestor tags (v0.1.1–v0.1.3) end up pointing at ambiguous commits after the merge (both ppc-prefixed and un-prefixed versions of the ancestor exist)** → **Mitigation**: Verify post-merge that v0.1.1–v0.1.3 resolve to the un-prefixed modern-side ancestor, not the ppc-prefixed rewrite. If they point at the rewritten commit, re-point them manually with `git tag -f v0.1.1 <correct-sha>`. This is a post-merge validation step in tasks.md.

**[Risk] The force-push window is destructive — anyone with a clone (even a stale one) will have broken pulls** → **Mitigation**: Solo project; confirm no outstanding clones outside the user's environment. Post-flip, document the recovery command (`git fetch && git reset --hard origin/main`) in the root README.

**[Risk] `git-filter-repo` not installed locally** → **Mitigation**: First task is verifying installation. If missing, install via `brew install git-filter-repo` on the modern Mac.

**[Risk] PPC build on the G4 breaks because the `powerpc-development` skill's hardcoded path is wrong** → **Mitigation**: Skill update is a listed followup task; not a blocker since the skill's ssh-connection logic is unchanged, only the project path needs bumping by one segment.

**[Risk] External references to PPC commit hashes (blog posts, docs, chat logs) become broken links** → **Mitigation**: Accepted. User confirmed no external SHA references exist that matter. Pre-flip `ppc-archive` branch keeps old hashes alive indefinitely as a lookup reference.

**[Risk] GitHub's default branch protection rules (if any) reject force-push** → **Mitigation**: Check GitHub repo settings pre-flight. Solo repo, likely no protection, but verify. Temporarily disable if needed; restore after.

**[Risk] Orphan tag v0.1.0 actually matters for something we don't know about** → **Mitigation**: Before dropping it, manually inspect the commit content (`git show v0.1.0`) and search the project docs/README for any mention of v0.1.0 as a milestone. Backup `legacy/v0.1.0` is pushed regardless, so recovery is trivial.

**[Risk] `test_threaded_news` committed to modern is actually source code, not a binary** → **Mitigation**: First task in the rewrite is verifying file type with `git show modern:test_threaded_news | file -`. If source, do not include in the purge. If binary, proceed. If both at different points in history, purge only the binary revs (advanced filter-repo usage).

## Migration Plan

**Phase 0 — Prep** (~5 min, reversible):
1. Verify working tree clean; resolve staged `RELEASE_NOTES_0.1.5.md` deletion and remove stray openspec swap file.
2. `git fetch origin` to ensure refs are current.
3. `git tag ppc-pre-flip HEAD && git push origin ppc-pre-flip`.
4. `git branch ppc-archive HEAD && git push -u origin ppc-archive`.
5. For `t in v0.1.0 v0.1.1 v0.1.2 v0.1.3 v0.1.4 v0.1.5 v0.1.6 v0.1.7; do git tag legacy/$t $t; done && git push origin --tags`.

**Phase 1 — Filter-repo on two scratches** (~10 min, reversible):
6. Clone repo to `/tmp/scratch-ppc`, checkout `main`, run filter-repo to prefix with `ppc/` and purge `mobius-hotline-server`.
7. Clone repo to `/tmp/scratch-modern`, checkout `modern`, verify `test_threaded_news` file type, run filter-repo to purge `Lemoniscate-0.1.5.dmg` (and `test_threaded_news` if binary).

**Phase 2 — Flip the real repo** (~5 min, reversible until push):
8. In real repo: `git fetch /tmp/scratch-modern` and `git fetch /tmp/scratch-ppc`.
9. `git reset --hard FETCH_HEAD` (pointing at scratch-modern's main).
10. `git merge --allow-unrelated-histories <scratch-ppc-head>` — creates the combined tree.

**Phase 3 — Post-merge cleanup** (~10 min):
11. `rm ppc/LICENSE`.
12. Rewrite `README.md` at root to explain layout.
13. Merge `.gitignore` rules, delete `ppc/.gitignore` if empty.
14. Commit cleanup as a single commit with clear message.
15. Validate: `git log --all --format='%H %s' | head`, `git log -- ppc/src/main.c` traces through history, no binary files in recent commits.

**Phase 4 — Tag reconciliation**:
16. For tags v0.1.4–v0.1.7: `git tag ppc/v0.1.4 v0.1.4 && git tag -d v0.1.4` etc.
17. Verify v0.1.1–v0.1.3 resolve to the correct un-prefixed modern-side commits; re-point manually if needed.
18. `git tag -d v0.1.0` (orphan; backed up as `legacy/v0.1.0`).

**Phase 5 — Publish** (point of no return):
19. `git push --force-with-lease origin main`.
20. `git push --force-with-lease --tags origin` (pushes rewritten tags + new `ppc/v0.1.x` + `legacy/*`).
21. `git push origin origin/modern:refs/heads/archive/pre-flip-modern`.
22. `git push origin --delete modern`.
23. Visual verification on GitHub: default branch shows modern at root, PPC under `ppc/`.

**Phase 6 — Followups (post-migration, not blocking)**:
24. Update `~/.claude/skills/powerpc-development/SKILL.md` project path.
25. Consider renaming the G4's SMB share dir from `lemoniscate-ppc` to `lemoniscate`.

**Rollback**:
- Before Phase 5: `git reset --hard ppc-pre-flip` restores the pre-flip state instantly. Scratches can be deleted.
- After Phase 5 but before new work lands: `git reset --hard ppc-pre-flip && git push --force-with-lease origin main && git push --force-with-lease --tags origin` restores the remote. `archive/pre-flip-modern` can be renamed back with `git push origin refs/heads/archive/pre-flip-modern:refs/heads/modern && git push origin --delete archive/pre-flip-modern`.
- After new work lands on the flipped `main`: rollback is no longer clean; case-by-case recovery required.
