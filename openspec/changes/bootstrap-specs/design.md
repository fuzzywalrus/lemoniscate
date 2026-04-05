## Context

Lemoniscate is an ~18K-line C/Obj-C Hotline server with two active branches: `main` (Tiger/PPC, GCC 4.0) and `modern` (macOS 10.11+, Clang/C11). The codebase has good inline documentation and maps to the Go-based Mobius server, but lacks formal behavioral specifications. Changes are validated primarily by a wire-format test suite and manual testing with Hotline clients.

The project has no existing OpenSpec directory — this is a greenfield bootstrap.

## Goals / Non-Goals

**Goals:**
- Establish formal capability specs covering all 8 core subsystems
- Create testable requirements with WHEN/THEN scenarios that can drive future test coverage
- Provide a spec baseline that makes behavioral regressions detectable across both branches
- Set up the OpenSpec workflow so future features and fixes go through spec-first review

**Non-Goals:**
- No code changes — specs describe existing behavior only
- No line-level API documentation (header comments already cover this)
- No specs for the GUI layer (AppKit admin interface) — it wraps the CLI server and is presentation-only
- No protocol extensions or new features proposed in this change
- No attempt to resolve differences between `main` and `modern` branches — specs target the `modern` branch behavior

## Decisions

### 1. Eight capability domains, not one monolithic spec
**Choice**: Split specs into 8 focused capability files aligned with the existing module boundaries (`src/hotline/` and `src/mobius/`).
**Rationale**: The codebase already has clean separation — wire protocol, auth, chat, files, users, news, networking, config. Mirroring this in specs keeps each file focused and independently reviewable. A single spec would be unwieldy at ~200+ requirements.
**Alternative considered**: Grouping by protocol layer (wire vs. application). Rejected because user-facing behavior doesn't always align with protocol layers (e.g., file transfer spans both).

### 2. Spec from headers and existing docs, not reverse-engineering behavior
**Choice**: Derive specs from the well-documented header files (which map 1:1 to Go structs) and existing `docs/` markdown.
**Rationale**: The headers contain authoritative wire formats, constants, and API contracts with Go-origin comments. This is more reliable than black-box testing.
**Alternative considered**: Running the server and capturing traffic. Rejected as slower and less complete — headers already document edge cases.

### 3. Modern branch as the spec target
**Choice**: Specs describe `modern` branch behavior (macOS 10.11+, CommonCrypto, C11).
**Rationale**: This is the actively developed branch. Tiger/PPC differences (OpenSSL vs CommonCrypto, deprecated API usage) are implementation variants, not behavioral differences.
**Alternative considered**: Dual-target specs with Tiger annotations. Rejected as premature — behavioral divergence is minimal.

### 4. Scenarios as acceptance tests, not integration tests
**Choice**: Spec scenarios describe observable behavior at capability boundaries (e.g., "WHEN client sends handshake THEN server replies with TRTP+0000"), not internal function calls.
**Rationale**: Specs should remain stable through refactoring. Internal function signatures may change; wire behavior and user-visible outcomes should not.

## Risks / Trade-offs

- **[Spec drift]** → Specs describe current behavior but code evolves. Mitigation: future changes go through `/opsx:propose` to update specs before implementation.
- **[Incomplete coverage]** → 8 capabilities may miss edge-case behaviors. Mitigation: specs are additive — requirements can be added as gaps are found.
- **[Branch divergence]** → Tiger branch may behave differently in subtle ways. Mitigation: specs target `modern` only; Tiger deltas can be annotated later if needed.
- **[Over-specification]** → Too-detailed specs become brittle. Mitigation: scenarios focus on observable outcomes, not implementation mechanics.
