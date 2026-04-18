## ADDED Requirements

### Requirement: Single long-lived branch with modern at root, PPC under `ppc/`

The repository SHALL have exactly one long-lived development branch (`main`). Modern code (Go, Dockerfile, modern macOS/Linux build) SHALL live at the root. PPC code (C99, Tiger/Leopard-targeted sources, Mac-specific Obj-C GUI) SHALL live under `ppc/`. Both codebases SHALL be reachable from a single checkout without branch switching.

#### Scenario: Fresh clone shows modern at root
- **WHEN** a user runs `git clone <repo>` and lists the top level
- **THEN** they see modern's source tree (`Makefile`, `Dockerfile`, `src/`, `scripts/`, etc.) at root
- **AND** they see a `ppc/` subdirectory containing the PPC C port

#### Scenario: PPC build happens under `ppc/`
- **WHEN** a user wants to build the PPC server
- **THEN** they `cd ppc` and run PPC-specific make targets
- **AND** they do not need to check out a different branch

#### Scenario: Modern build happens at root
- **WHEN** a user wants to build the modern server
- **THEN** they run modern build commands at the repo root
- **AND** they do not need to `cd` into a subdirectory

### Requirement: No committed binary artifacts in history

The repository SHALL NOT contain committed compiled binaries, release archives, or test executables anywhere in its history on the `main` branch. Release artifacts SHALL be delivered through GitHub Releases or similar out-of-tree distribution, never committed to the repo.

#### Scenario: History contains no known binary artifact names
- **WHEN** `git log --all --diff-filter=A -- mobius-hotline-server Lemoniscate-0.1.5.dmg test_threaded_news` is run on the post-flip repo
- **THEN** the output is empty
- **AND** the repo's `.git` pack size reflects at least 5 MB recovered vs. the pre-flip size

#### Scenario: `.gitignore` prevents future binary commits
- **WHEN** a developer tries to `git add` a compiled binary matching common patterns (`*.app/`, `*.dmg`, `*.zip`, bare-named executables under project root)
- **THEN** `.gitignore` rules SHALL exclude them from staging by default

### Requirement: Tag naming convention

Tags SHALL follow a three-tier naming convention that makes the release scope explicit:

1. **Unified releases** (both modern and PPC ship together post-flip): `v<major>.<minor>.<patch>` — e.g., `v0.2.0`.
2. **PPC-specific releases** (tagged on `main` that describe PPC-only release content, including all pre-flip PPC tags v0.1.4–v0.1.7): `ppc/v<major>.<minor>.<patch>` — e.g., `ppc/v0.1.5`.
3. **Legacy backup tags** (pre-flip safety nets for all 8 historical tags): `legacy/v<major>.<minor>.<patch>` — e.g., `legacy/v0.1.0`. These point at pre-flip commits and are never moved or deleted.

Ancestor tags `v0.1.1`, `v0.1.2`, `v0.1.3` SHALL remain plain because they predate the branch divergence and validly identify releases on both lines.

#### Scenario: Post-flip tag set is clean
- **WHEN** `git tag -l` is run after migration
- **THEN** the output contains: `v0.1.1`, `v0.1.2`, `v0.1.3` (plain ancestor tags), `ppc/v0.1.4` through `ppc/v0.1.7` (renamed PPC-line), `legacy/v0.1.0` through `legacy/v0.1.7` (backups)
- **AND** the output does NOT contain plain `v0.1.4`, `v0.1.5`, `v0.1.6`, `v0.1.7`, or plain `v0.1.0`

#### Scenario: Legacy tags are immutable
- **WHEN** a new release ships post-flip
- **THEN** tags under `legacy/` are not modified or moved
- **AND** they continue to resolve to pre-flip commit hashes reachable via the `ppc-archive` branch

### Requirement: Backport workflow is in-tree

PPC backports of modern features SHALL be implemented as a single commit (or single PR) that modifies both `src/` (modern) and `ppc/src/` (PPC) in the same changeset. Cross-branch cherry-picks SHALL NOT be used as the mechanism for backports.

#### Scenario: A new feature is backported
- **WHEN** a developer adds a feature to modern and decides to backport it to PPC
- **THEN** the resulting PR contains changes to both `/src/…` paths (modern) and `/ppc/src/…` paths (PPC)
- **AND** the commit message references both codebases

#### Scenario: The `modern` branch name is retired
- **WHEN** a developer tries to push to `origin/modern`
- **THEN** the branch does not exist (or exists as a read-only archive named `archive/pre-flip-modern`)
- **AND** new feature work lands on `main` only

### Requirement: Backup refs are pushed before destructive operations

Before any force-push or history rewrite, the repository SHALL have backup refs pushed to origin:

1. `ppc-pre-flip` tag pointing at pre-migration `main` tip.
2. `ppc-archive` branch preserving the pre-migration `main` line.
3. `legacy/v0.1.x` tags (all 8) preserving pre-migration tag targets.
4. `archive/pre-flip-modern` branch preserving the pre-migration `modern` ref.

All four SHALL exist on `origin` before Phase 5 (publish) begins, and SHALL persist indefinitely as archaeology references.

#### Scenario: A post-flip rollback is possible
- **WHEN** a developer needs to understand what `main` looked like before the migration
- **THEN** they can `git checkout ppc-archive` to see the pre-flip PPC tree at its original commits
- **AND** they can `git checkout archive/pre-flip-modern` to see the pre-flip modern tree

#### Scenario: Pre-push abort preserves state
- **WHEN** the migration is aborted before Phase 5 (publish)
- **THEN** running `git reset --hard ppc-pre-flip` in the local repo fully restores the pre-migration state
- **AND** no remote state has been modified

### Requirement: `openspec/` directories remain per-codebase

The repository SHALL maintain two independent `openspec/` change-management roots: `/openspec/` for modern, and `/ppc/openspec/` for PPC. They SHALL NOT be unified or cross-referenced; each tracks changes scoped to its own codebase.

#### Scenario: Modern change proposal lives at root openspec
- **WHEN** a developer creates a new OpenSpec change affecting modern
- **THEN** it lands under `/openspec/changes/<name>/`
- **AND** its specs reference modern's capabilities

#### Scenario: PPC change proposal lives under ppc/openspec
- **WHEN** a developer creates a new OpenSpec change affecting PPC
- **THEN** it lands under `/ppc/openspec/changes/<name>/`
- **AND** its specs reference PPC's capabilities
