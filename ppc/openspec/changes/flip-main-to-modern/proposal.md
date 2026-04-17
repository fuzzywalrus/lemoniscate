## Why

The current two-branch layout (`main` = PPC C port, `modern` = modern Go codebase) misrepresents the actual development flow. Features originate on `modern` and get backported to `main` as PPC-compatible C code. Modern is effectively first-class, PPC is the maintained-legacy backport target — but the branch names and `main`-as-PPC default signal the opposite. Ahead/behind counts on GitHub mislead because every backported feature shows up twice (once per branch), and modern's history is "buried" behind a branch switch. Unifying to a single `main` with modern at root and PPC under `ppc/` makes the hierarchy match reality, enables cross-codebase PRs for backport commits, and clarifies release parity between modern and retro.

## What Changes

- **BREAKING**: Rewrite `main`'s commit history. Current `main` contents (PPC tree) move to `ppc/` subdirectory via `git filter-repo --to-subdirectory-filter`; SHAs on those commits change.
- **BREAKING**: Rewrite `modern`'s commit history to purge committed binary `Lemoniscate-0.1.5.dmg` and (if present) `test_threaded_news`. Modern-side SHAs change as a result.
- Replace `main` with rewritten-modern, then merge rewritten-PPC under `ppc/` with `--allow-unrelated-histories`. Final `main` has modern at root and PPC under `ppc/`, with both histories preserved and joined at a single merge commit.
- Purge committed binaries from PPC history (`mobius-hotline-server` — 7 historical copies) in the same filter-repo pass that adds the `ppc/` prefix.
- Back up all 8 existing tags (v0.1.0 – v0.1.7) under `legacy/v0.1.x` prefix before any destructive operation.
- Rename PPC-line tags v0.1.4 – v0.1.7 to `ppc/v0.1.x` post-flip (they tagged PPC-specific releases). Ancestor tags v0.1.1 – v0.1.3 stay plain (they predate divergence and remain valid for both lines).
- Drop the orphan tag `v0.1.0` (points at commit `3723cdf`, not reachable from any branch) after verifying no external references.
- Rename `origin/modern` to `archive/pre-flip-modern` so the branch name is preserved as a read-only reference; delete `origin/modern` after verification.
- Delete duplicate `ppc/LICENSE` (identical to root).
- Rewrite root `README.md` to document the new layout and point users to `ppc/README.md` for PPC-specific build instructions.
- Merge `.gitignore` rules so both codebases' ignores apply at the root level where relevant.
- **BREAKING** (workflow): Backport PRs going forward touch both `/src/…` (modern) and `/ppc/src/…` (PPC) in a single commit rather than being two separate cross-branch cherry-picks.

## Capabilities

### New Capabilities
- `repo-layout`: Canonical rules for how the monorepo is organized — which codebase lives where, branch/tag naming conventions, backport workflow, and cleanup invariants for committed binaries. Becomes the durable reference for the post-flip structure.

### Modified Capabilities
<!-- None. No existing spec in openspec/specs/ to modify. -->

## Impact

- **Code paths**: All PPC source files move from `src/…` to `ppc/src/…` in the working tree. Modern files arrive at root. IDE bookmarks, scripts, and external links that reference `src/` paths on the current `main` will need updating.
- **Commit history**: All commits ever made on `main` get new SHAs (filter-repo rewrite). All commits on `modern` get new SHAs (DMG purge). Existing local clones of the repo will have diverged histories and must be reset.
- **Tags**: 8 tags are renamed or dropped. `legacy/` backup tags are pushed to origin for safety. External links to GitHub's tag pages (`/releases/tag/v0.1.x`) continue to work because GitHub resolves tag names at render time.
- **Remote branches**: `origin/modern` is renamed/deleted; `origin/main` is force-pushed.
- **Repo size**: ~5.7 MB recovered from history (binary artifacts purged across both sides).
- **`~/.claude/skills/powerpc-development` skill**: Project path changes from `/Users/me/Public/lemoniscate-ppc` to `/Users/me/Public/lemoniscate-ppc/ppc` — skill file needs updating.
- **Optional**: SMB share top-level directory name (`lemoniscate-ppc`) no longer reflects its contents; rename to `lemoniscate/` is a followup.
- **Dependencies / tooling**: Requires `git-filter-repo` installed locally (not shipped with git). No CI to migrate (neither branch has `.github/workflows/`). No submodules, no LFS.
- **Rollback**: Pre-flip backup tag `ppc-pre-flip` + branch `ppc-archive` pushed to origin before any history rewrite. `legacy/` tag backups pushed before destructive tag operations. Pre-push rollback is `git reset --hard ppc-pre-flip`; post-push rollback is the same plus a force-push.
