# 17 app-h-dead-ifdef-cleanup -refactor

**Type:** refactor
**Priority:** 17 (low effort / low risk)
**Source:** Claude review (review-latest.claude.md)

## Problem

`app/app.h` lines 13–17 contain an `#ifdef __APPLE__ / #else / #endif` block with only comment text and no actual code:

```cpp
#ifdef __APPLE__
// Metal renderer header...
#else
// Vulkan renderer header...
#endif
```

These are leftover scaffolding from a refactor. They add noise, give false signals to readers expecting platform-specific includes, and can mislead tooling (e.g., IDEs that collapse `#ifdef` blocks).

## Acceptance Criteria

- [x] Read `app/app.h`.
- [x] Remove the empty comment-only `#ifdef` blocks.
- [x] If the comment intent was to document where renderer headers *used to be* included, move that documentation to a brief comment outside the `#ifdef`.
- [x] Build both targets to confirm nothing breaks.

## Implementation Notes

- This is a one-minute change. No architecture decisions needed.
- Run `clang-format` after the edit (pre-commit hook will enforce this anyway).

## Interdependencies

- No blockers. Completely independent.

---

*Authored by claude-sonnet-4-6 — 2026-03-23*
