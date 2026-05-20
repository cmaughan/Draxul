# Refactor: Unify Physical/Logical Pixel Conversion

**Type:** refactor
**Priority:** 16
**Source:** Claude review

## Problem

The physical ↔ logical pixel conversion is reinvented in two places:

1. `InputDispatcher::to_physical()` — a private helper method that multiplies a logical position by the DPI scale factor.
2. An inline lambda (or equivalent) in `App::wire_window_callbacks` that does the same multiplication.

Two implementations mean two places to update if the scaling model changes (e.g. per-monitor DPI, fractional scaling). They can also diverge silently.

## Investigation steps

- [x] Read `libs/draxul-nvim/src/input_dispatcher.cpp` (or wherever `to_physical` lives) — find its signature and implementation.
- [x] Read `app/app.cpp` — find the inline lambda / duplicate conversion in `wire_window_callbacks`.
- [x] Check whether any other sites do inline `* scale` or `/ scale` conversions.

## Proposed design

A tiny utility in `libs/draxul-types/include/draxul/pixel_scale.h`:

```cpp
struct PixelScale {
    float factor = 1.0f;

    // Convert a logical-space position to physical pixels.
    glm::vec2 to_physical(glm::vec2 logical) const { return logical * factor; }

    // Convert a physical-space position to logical pixels.
    glm::vec2 to_logical(glm::vec2 physical) const { return physical / factor; }
};
```

Since `draxul-types` is header-only and has no deps, this is the right home.

## Implementation steps

- [x] Create `pixel_scale.h` in `libs/draxul-types/include/draxul/`.
- [x] Replace `InputDispatcher::to_physical()` with `PixelScale::to_physical()`.
- [x] Replace the lambda/inline conversion in `App::wire_window_callbacks`.
- [x] Search for other inline `* scale` patterns in `app/` and `libs/` and migrate them.
- [x] Build and verify no regressions.

## Acceptance criteria

- [x] Single canonical `PixelScale` struct used at all conversion sites.
- [x] No inline `* scale` or `/ scale` for pixel conversion outside the struct.
- [x] Builds clean; existing tests pass.

## Interdependencies

None — standalone, low-risk.

---
*Filed by `claude-sonnet-4-6` · 2026-03-26*
