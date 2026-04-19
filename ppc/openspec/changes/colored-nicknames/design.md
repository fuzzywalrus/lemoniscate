## Context

This design document is a **thin companion** to the modern sibling's `design.md` at `/openspec/changes/colored-nicknames/design.md`. The implementation files under `ppc/src/hotline/` and `ppc/src/mobius/` are intended to be byte-identical to their modern counterparts, so the decisions captured in the modern design apply verbatim here. This file captures only the items that differ on the PPC target.

Read the modern sibling first for:

- `DATA_COLOR` field format and wire handling.
- `color_aware` flag semantics.
- The 5-step color resolution cascade (`hl_nick_color_resolve`).
- Mode enforcement for `off` / `server_only` / `user_choice`.
- Account class detection (Option A — exact permission-set match).
- YAML parsing of `Color` and `ColoredNicknames:` section.
- Transaction handler changes for 304, 301, 117, user self-info, user list.
- Plist persistence keys (`ColoredNicknamesMode`, `DefaultAdminColor`, `DefaultGuestColor`).

## PPC-Specific Constraints

### Tiger SDK and gcc-4.0 compatibility

- `NSColorWell` has been available since macOS 10.0. No compatibility shim needed for Tiger (10.4) or Leopard (10.5).
- The C code uses only C99 constructs and `#include`s already present in the PPC codebase (`stdint.h`, `string.h`, `stdbool.h`). No new header dependencies.
- No pragma-guarded features. The file set is identical to modern.

### Endianness

PPC is big-endian. The `DATA_COLOR` wire payload is `0x00RRGGBB` big-endian. In-memory representation as `uint32_t` matches the wire layout directly on PPC — no byte-swap is strictly required when reading or writing the network bytes into memory as a native `uint32_t`.

Nonetheless, the code SHALL call `htonl`/`ntohl` at the (de)serialization boundary. This preserves source parity with the modern sibling (where the macros do real work). The PPC compiler emits these as no-ops, so there is no performance cost.

### GUI section reuse of existing disclosure pattern

The PPC GUI already ships the disclosure-section pattern in 0.1.7 for Mnemosyne and Encoding settings. Adding the "Colored Nicknames" disclosure follows the same layout flow (`AppController+LayoutAndTabs.inc`) with no structural changes. Account Editor color row fits between the existing Name/File Root row and Template popup, matching the modern sibling's placement.

### Access template source-of-truth sharing

The modern design's task 12 migrates the GUI's `adminAccessTemplate` / `guestAccessTemplate` away from hand-coded sets in `AppController+AccountsData.inc` toward constants defined in `include/hotline/access.h`. The PPC sibling performs the same migration in `ppc/src/gui/AppController+AccountsData.inc` referencing `ppc/include/hotline/access.h`. Since `access.h` is required to be source-identical between the two codebases, a single constant definition serves both GUIs and both servers once this migration completes.

### Build integration

`ppc/Makefile` gains one new source entry (`ppc/src/hotline/access.c`). The build already compiles all files in `ppc/src/hotline/` via the same object-list generator as modern, so a Makefile edit may not be needed if the glob pattern picks up the new file automatically. Verify during implementation.

### Testing constraints

The modern sibling adds unit tests (`test_access.c`, `test_nick_color.c`, extensions to `test_yaml_account.c`). The PPC sibling builds the same test files. PPC's `make test` target wires them in per the existing convention. No mocking or test framework change required; the PPC test harness already supports plain `assert()`-based tests.

## Open Questions Specific to PPC

- **Q1: Should the PPC `hl_access_t` constants be preprocessor `#define`s or `static const`?** Modern uses `static const`. PPC gcc-4.0 accepts both, but some older callers in `transaction_handlers_clean.c` use the values in `switch` cases, which requires `#define` (constant expressions). Resolve by checking existing `ppc/include/hotline/access.h` conventions and matching them.

- **Q2: Test harness discovery.** The modern sibling's test runner picks up new `test/*.c` files automatically; the PPC test Makefile may require manual wiring. This is an implementation-time detail, not a design concern — flagged here for the implementer.

## Risks Specific to PPC

- **Tiger Aqua visual quirks.** The color well on Tiger 10.4 Aqua theme may render slightly differently than Leopard or modern. This is cosmetic; operator workflow is unaffected.
- **Source-parity drift during implementation.** If the modern and PPC implementations are authored separately in different PRs, subtle differences in `client_conn.c` or `yaml_account_manager.c` can accumulate. Mitigation: the repo-layout spec's single-commit backport rule. Reviewer discipline enforces the rest.
