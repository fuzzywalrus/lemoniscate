## Superseded by `flip-main-to-modern`

This change captured the **pre-flip** strategy for keeping PPC in sync with modern: manually port modern's changes into the PPC `main` branch, subsystem by subsystem, commit by commit. 48 of 63 tasks were actually executed under this plan and shipped real work (platform layer, Mnemosyne, storage readers, UTF-8 fixes, GUI parity).

The April 2026 repo flip (`flip-main-to-modern`, archived 2026-04-17) retired this approach entirely. Both codebases now live on a single `main` with modern at root and PPC under `ppc/`. The authoritative workflow rule is now captured in the `repo-layout` capability:

> **Backport workflow is in-tree** — PPC backports of modern features SHALL be implemented as a single commit (or single PR) that modifies both `src/` (modern) and `ppc/src/` (PPC) in the same changeset. Cross-branch cherry-picks SHALL NOT be used as the mechanism for backports.

### What happened to the remaining 15 tasks

The unfinished items in `tasks.md` (endianness audits, test Makefile wiring, CHANGELOG/README updates, live-instance testing) were never formally retired — they were simply overtaken by the flip. Most are no longer framed the same way:

- **Endianness audits (2.2, 2.13, 2.20)** — still relevant as general PPC hygiene, but are codebase-wide concerns, not port-specific tasks. Tracked informally.
- **Test Makefile wiring (6.4, 6.5)** — still relevant; the tests are ported but not hooked into `make`. Open as a standalone cleanup whenever someone picks it up.
- **CHANGELOG/README updates (6.9, 6.10)** — README was refreshed in the flip; CHANGELOG work is ongoing per release cadence.
- **Live testing (6.14, 6.15, 6.16)** — Mnemosyne sync live test, threaded news round-trip, message board round-trip. Also codebase-wide concerns.
- **Vespernet on PPC (6.17)** — unresolved investigation, now a standalone issue.
- **Help button tooltips (6.19)** — cosmetic Tiger Aqua issue, tracked informally.

### Why archive rather than finish

Finishing the remaining tasks under this change's framing would imply the change itself is still active. It isn't. The framing ("port X subsystem from modern → PPC") was replaced by the in-tree backport model. Carrying open items under a dead frame is noise; reframing them as standalone issues when someone actually picks them up is cleaner.
