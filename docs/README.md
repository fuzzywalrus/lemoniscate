# Lemoniscate Documentation

## Start here

- New users: use the quickstart in the project [README](../README.md).
- Server operators: see [SERVER.md](SERVER.md).
- Security setup: see [SECURITY.md](SECURITY.md).
- GUI users: see [GUI.md](GUI.md).

## Guides by role

- Operator
  - [Server reference](SERVER.md): CLI flags, config layout, protocol/transaction coverage, and build notes.
  - [Security & encryption](SECURITY.md): HOPE, TLS, E2E file gating.
  - [GUI reference](GUI.md): app build, launch behavior, limitations, troubleshooting.
- Developer
  - [Project README](../README.md): repo layout, build targets, current feature status.

## Notes on project status

Documentation in this folder is aligned with the current implementation state:
- Some subsystems are complete and usable now.
- Some are partial/stubbed and explicitly marked as such.
- Planned work remains called out to avoid expectation mismatch.

## Branch Strategy

This project maintains two active branches:

- **`main`** — Targets Mac OS X 10.4 Tiger (PowerPC). Uses GCC 4.0, OpenSSL, `OSAtomic`, and Tiger-era AppKit APIs. This is the original build.
- **`modern`** — Targets macOS 10.11+ (Intel/Apple Silicon). Uses Clang, C11, CommonCrypto, `<stdatomic.h>`, and current AppKit APIs. Zero deprecation warnings.

### Syncing changes from main → modern

Changes on `main` are manually re-implemented on `modern` with modern API equivalents, then a `git merge -s ours` records the parity without overwriting modern code. This means:

1. New features and fixes on `main` are ported to `modern` by hand, translating any deprecated APIs to their modern replacements.
2. After porting, `git merge origin/main -s ours` is run. This creates a merge commit that tells git "we've accounted for everything on main" — but keeps all files exactly as they are on `modern`.
3. Future merges from `main` will only surface *new* commits after the last parity merge.

See [INTEGRATION_NOTES.md](../INTEGRATION_NOTES.md) for a detailed record of what was integrated vs. skipped and why.

### Why not a regular merge?

A regular merge would reintroduce deprecated constants (`NSOnState`, `NSRoundedBezelStyle`, etc.), Tiger backfill categories, OpenSSL dependencies, and other legacy code that the modern branch specifically removed. The `ours` strategy preserves git's understanding of commit history without reverting the modernization work.

## Related

- [Changelog](../CHANGELOG.md)
- [Integration Notes](../INTEGRATION_NOTES.md)
