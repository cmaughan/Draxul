# 00 hostcontext-reference-members -bug

**Type:** bug
**Priority:** 0 (highest — undefined behaviour risk)
**Source:** Claude review (review-latest.claude.md)

## Problem

`HostContext` stores `IWindow&`, `IGridRenderer&`, and `TextService&` as C++ reference members. References cannot be null-checked after construction, and a caller that passes a dangling or prematurely-destroyed object produces undefined behaviour with no compile-time or runtime diagnostic.

Key risks:
- Any future refactor that reorders or delays subsystem construction can silently produce dangling references.
- A test that creates a `HostContext` with a temporary will compile but crash at runtime under some compilers.
- Adding new constructors or copy/move operations is outright broken — reference members make the class non-copyable and non-movable in non-obvious ways.

## Acceptance Criteria

- [x] Locate `HostContext` definition (likely `libs/draxul-host/include/draxul/host_context.h` or similar).
- [x] Replace reference members with non-owning (raw) pointers: `IWindow*`, `IGridRenderer*`, `TextService*`.
- [x] Add a construction-time assertion (or factory function) that checks all pointers are non-null.
- [x] Update all construction sites (`app.cpp`, any test harnesses) to pass pointers.
- [x] Confirm no member access site forgets to check for null (they should all be safe after the assertion at construction).
- [x] Build both macOS and (if available) Windows targets; run `ctest`.

## Implementation Notes

- Prefer `DRAXUL_ASSERT(ptr != nullptr)` at the end of the constructor rather than a factory pattern — keep the change minimal.
- Do not change the ownership model; these are non-owning observers. The lifetime guarantee remains the caller's responsibility, but at least null is now a diagnosable failure.
- If a sub-agent does this work: read `host_context.h` first, then grep for all construction sites before changing anything.

## Interdependencies

- No blockers. This is a standalone correctness fix.
- Completing this makes it safer to add new host types (future work).

---

*Authored by claude-sonnet-4-6 — 2026-03-23*
