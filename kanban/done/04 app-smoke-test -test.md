# 04 app-smoke-test -test

**Status:** Complete

**Priority:** HIGH
**Type:** Test (end-to-end orchestrator coverage)
**Raised by:** Claude
**Model:** claude-sonnet-4-6

---

## Problem

There is no test that exercises `App::initialize()` → `App::pump_once()` × N → `App::shutdown()` using the existing `FakeWindow` and `FakeRenderer` stubs. The orchestrator (`app/app.h/cpp`) is the most complex wiring point in the codebase; breakage in startup/shutdown sequencing or frame loop panics surfaces only at runtime. An automated smoke test would catch these in CI before they reach humans.

---

## Implementation Plan

- [x] Read `app/app.h` and `app/app.cpp` to understand `App::initialize()`, `pump_once()`, and `shutdown()` signatures and dependencies.
- [x] Read `tests/fakes/` — locate `FakeWindow`, `FakeRenderer`, and any existing fake Neovim process stubs.
- [x] Read `tests/CMakeLists.txt` to understand how to add the new test file to the build.
- [x] Design the test:
  - Construct `App` with `FakeWindow` + `FakeRenderer` + a fake or null `NvimProcess` (or a loopback pipe that immediately exits).
  - Call `App::initialize()` — assert it returns success.
  - Call `App::pump_once()` a small number of times (e.g., 3) — assert no crash, no assertion failure.
  - Call `App::shutdown()` — assert clean teardown.
- [x] If `App` does not currently accept injected stubs (i.e., it constructs its own concrete subsystems), a small refactor of the constructor/factory may be needed first — discuss with the user before proceeding.
  - Added `host_factory` override to `AppOptions` (analogous to existing `window_factory` and `renderer_create_fn`).
  - Wired into `HostManager::create_host_for_leaf()` to use the factory when set.
- [x] Add the test file to `tests/CMakeLists.txt`.
- [x] Build and run: `cmake --build build --target draxul-tests && ctest`.

---

## Acceptance

- The test builds and passes on both macOS and Windows (or is platform-guarded where fake stubs are platform-specific).
- `App::initialize()` failure (e.g., window creation fails) is tested as a second case that returns an error without crashing.
- The test can serve as the foundation for future multi-pane and lifecycle regression tests.

---

## Interdependencies

- `11-refactor` (pump_once decomposition) — decomposed helpers make it easier to stub individual stages, but this test can be written before the refactor.
- `03-test` (HostManager lifecycle tests) — complementary; can be done in parallel.
- Requires existing `FakeWindow` / `FakeRenderer` stubs to be usable from the test target.

---

*claude-sonnet-4-6*
