# 27 hostmanager-unsafe-downcast -bug

*Filed by: claude-sonnet-4-6 — 2026-03-29*
*Source: review-latest.claude.md [C]*

## Problem

`app/host_manager.cpp:283` contains:

```cpp
static_cast<I3DRenderer*>(deps_.grid_renderer)
```

`deps_.grid_renderer` is typed as `IGridRenderer*`.  The cast assumes `IGridRenderer` inherits
from `I3DRenderer`, which is currently true, but `static_cast` bypasses runtime type checking.
If the inheritance chain ever changes (or the pointer is null in an unexpected path), this
becomes undefined behaviour with no diagnostic — silently corrupting the call rather than
producing a compile error or a checked null.

## Acceptance Criteria

- [x] The cast is replaced with `dynamic_cast<I3DRenderer*>(deps_.grid_renderer)` plus a null
      check (assert or early-return with a logged error).
- [x] If the cast succeeds, behaviour is identical to today.
- [x] A compile-time `static_assert` or comment documents *why* this cast is expected to
      succeed (to prevent a future developer from converting it back to `static_cast`).
- [x] Existing smoke tests and render tests pass.

## Implementation Plan

1. Read `app/host_manager.cpp` — confirm the exact location and the surrounding control flow.
2. Replace the `static_cast` with `dynamic_cast<I3DRenderer*>(...)`.
3. Add: `DRAXUL_ASSERT(renderer_3d != nullptr, "IGridRenderer must implement I3DRenderer");`
   (or equivalent) immediately after the cast.
4. Run `cmake --build build --target draxul draxul-tests && py do.py smoke`.

## Files Likely Touched

- `app/host_manager.cpp`

## Interdependencies

- Coordinate with icebox `16 hostmanager-dynamic-cast-removal -refactor` — that item removes all
  `dynamic_cast` capability probing; this bug fix is a prerequisite safety improvement that
  should land first regardless of whether the icebox item is ever pulled.
- No test file changes needed here; the existing smoke test covers the startup path.
