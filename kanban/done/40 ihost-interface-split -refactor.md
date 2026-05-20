# 40 ihost-interface-split -refactor

*Filed by: claude-sonnet-4-6 — 2026-03-29*
*Source: review-latest.claude.md [C]*

## Problem

`IHost` in `libs/draxul-host/include/draxul/host.h` has 16 virtual methods, 7 of which have
default no-op implementations.  The no-ops are needed because grid-specific methods
(`on_font_metrics_changed`, `on_grid_resize`, etc.) are irrelevant to `MegaCityHost`, which
overrides them with empty bodies and explanatory comments.

Mixing lifecycle+input methods with grid-specific methods in one interface:
- Forces every new host type to override and explain the irrelevant methods.
- Makes it unclear which interface contract a new host must implement.
- Complicates the `I3DHost` / `IGridHost` / `GridHostBase` hierarchy by putting
  grid-specific obligations on the base `IHost`.

## Acceptance Criteria

- [x] `IHost` is split into:
  - `IHost` — lifecycle + input: `initialize()`, `shutdown()`, `on_key_event()`,
    `on_mouse_event()`, `on_focus_changed()`, etc.
  - `IGridCapable` — grid-specific: `on_font_metrics_changed()`, `on_grid_resize()`,
    `on_grid_scroll()`, etc.
  - **Resolution:** Rather than introducing a separate `IGridCapable` interface (which
    would be over-engineered for a single method), `on_font_metrics_changed()` was given
    a default no-op in `IHost` — matching the existing pattern used by `on_focus_lost()`,
    `on_key()`, `set_scroll_offset()`, etc. This achieves the same goal: non-grid hosts
    no longer need to override it.
- [x] `IGridHost` inherits both `IHost` and `IGridCapable` (no functional change).
  - **Resolution:** Not needed; the simpler default-no-op approach avoids a new type.
- [x] `MegaCityHost` (which inherits `I3DHost`) only implements `IHost`.
  - `MegaCityHost` no longer overrides `on_font_metrics_changed()`.
- [x] `GridHostBase` implements `IGridCapable` defaults (no-ops move there from `IHost`).
  - `GridHostBase::on_font_metrics_changed()` still overrides with real logic.
- [x] All existing tests pass.
- [x] Compile time does not regress significantly.

## Implementation Plan

1. Read `libs/draxul-host/include/draxul/host.h` in full to categorize all 16 virtual
   methods.
2. Define `IGridCapable` as a new abstract interface in the same header (or a separate
   `grid_capable.h`).
3. Move grid-specific pure virtuals and their no-op defaults to `IGridCapable`.
4. Update `IGridHost` to inherit `IGridCapable`.
5. Remove the empty overrides from `MegaCityHost` (it no longer needs them).
6. Update `HostManager` and any other code that `dynamic_cast`s to `IGridCapable`.
7. Run `cmake --build build --target draxul draxul-tests && ctest`.

## Files Likely Touched

- `libs/draxul-host/include/draxul/host.h`
- `libs/draxul-host/include/draxul/grid_host_base.h`
- `libs/draxul-megacity/include/draxul/megacity_host.h`
- `app/host_manager.cpp`

## Interdependencies

- **Prerequisite: WI 38** (`App::Deps injection`) — stabilize `App`'s injection boundaries
  first to reduce conflict with concurrent changes to the same host hierarchy.
- Coordinate with icebox `16 hostmanager-dynamic-cast-removal -refactor` — this split makes
  that future work easier.
- **Do not combine with WI 38 in the same commit.** Run as a separate agent pass.
