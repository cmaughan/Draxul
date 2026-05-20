# 38 app-deps-injection -refactor

*Filed by: claude-sonnet-4-6 — 2026-03-29*
*Source: review-latest.claude.md [C] (highest-leverage refactor); review-latest.gpt.md [P]*

## Problem

`app/app.cpp` (~867 lines) is the orchestrator for window, renderer, text service, UI panel,
host manager, input dispatcher, and GUI action handler.  It is currently untestable in
isolation because:

1. There is no `AppDeps` struct — all dependencies are created internally with direct
   `new` / factory calls.
2. Unit tests cannot inject fakes for any subsystem.
3. Every other major subsystem (`HostManager`, `GridRenderingPipeline`, etc.) already has a
   `Deps` struct; `App` is the outlier.

As a result, initialization rollback, `pump_once` frame gating, and shutdown ordering are
only covered by end-to-end smoke tests, not isolated unit tests.

This is the highest-leverage refactor in the codebase for improving testability.

## Acceptance Criteria

- [x] An `AppDeps` struct (or equivalent) exists, containing injected pointers/refs for:
      `IWindow`, `IGridRenderer`, `TextService`, host creation factory, input dispatcher
      factory (or pre-built instances).
- [x] `App` has a constructor that accepts `AppDeps` (in addition to or replacing the current
      internal-creation path).
- [x] The main binary path (in `app/main.cpp`) constructs `AppDeps` from real implementations
      and passes it to `App` — no functional change to the shipped binary.
- [x] At least one new unit test exercises `App::initialize()` failure rollback using fake
      deps (e.g., a renderer that fails to initialize).
- [x] All existing smoke, render, and VT tests pass.

## Implementation Plan

1. Read `app/app.h` and `app/app.cpp` in full to catalogue all direct-construction calls.
2. Define `struct AppDeps` in `app/app.h` with all injectable interfaces.
3. Add an `App(AppDeps deps)` constructor that stores the deps rather than constructing them.
4. Preserve the existing `App()` constructor as a convenience wrapper that creates real deps —
   or update `main.cpp` to build `AppDeps` explicitly (preferred for clarity).
5. Write one unit test: `App_initialize_renders_failure_rollback` using `FakeRenderer` that
   returns `false` from `initialize()`.
6. Run `cmake --build build --target draxul draxul-tests && py do.py smoke`.

## Files Likely Touched

- `app/app.h` — add `AppDeps` struct
- `app/app.cpp` — major refactor of constructor and init paths
- `app/main.cpp` — update to construct `AppDeps` explicitly
- `tests/app_deps_tests.cpp` (new)

## Interdependencies

- **Prerequisite for WI 40** (`ihost-interface-split`) — the split touches all hosts and is
  easier after `App` has stable injection boundaries.
- **Enables** future `App` unit tests beyond the first rollback test.
- `app/app.cpp` is a high-conflict file — do not combine this WI with other active `app/`
  changes.  **Run as a solo agent pass.**
- Coordinate with active `14 config-layer-decoupling -refactor` if config construction is
  also being reorganized.
