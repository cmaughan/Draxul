# 23 app-testable-window-renderer -refactor

**Priority:** MEDIUM
**Type:** Refactor (testability)
**Raised by:** Chris + Claude
**Model:** claude-opus-4-6

---

## Problem

`App` owns a concrete `SdlWindow` member and constructs a concrete `RendererBundle` via `create_renderer()`. The extracted methods (`close_dead_panes`, `render_frame`, `render_imgui_overlay`) can't be unit-tested because constructing an `App` requires a real GPU-backed window and renderer.

The `AppOptions` injection points (`window_init_fn`, `renderer_create_fn`) only control *how* the concrete objects are initialized — they don't substitute test doubles. This means:
- No unit tests for the pump loop (dead pane cleanup, frame request flow, shutdown transitions)
- No unit tests for initialization sequencing (rollback on partial failure)
- Smoke tests require a real display server (won't run in headless CI without Xvfb or similar)

---

## Root Cause

`App` uses `SdlWindow` (concrete) instead of `IWindow` (interface). Six methods that `App` calls are SdlWindow-only and not on `IWindow`:

| Method | Used by |
|--------|---------|
| `wake()` | `request_frame()`, `HostCallbacks` |
| `activate()` | `pump_once()` window activation |
| `wait_events(int)` | `pump_once()` event wait |
| `set_clamp_to_display(bool)` | `initialize()` |
| `set_hidden(bool)` | `initialize()` (render tests) |
| `set_size_logical(int,int)` | `normalize_render_target_window_size()` (render tests) |

`RendererBundle` is already abstract internally (it wraps a `unique_ptr<IRenderer>` and upcasts to `IGridRenderer*`, `IImGuiHost*`, `ICaptureRenderer*`). The issue is that `App` owns it by value and populates it via `create_renderer()`, which is a concrete factory.

---

## Implementation Plan

### Phase 1: Widen IWindow

- [x] Add `wake()`, `activate()`, `wait_events(int timeout_ms) -> bool` as virtual methods on `IWindow` with default no-op/true implementations (so existing `FakeWindow` compiles without changes).
- [x] Move `set_clamp_to_display()` and `set_hidden()` to `IWindow` with default no-ops (these are init-time config, harmless to ignore in tests).
- [x] `set_size_logical()` is only used in the render test normalization helper — leave it on `SdlWindow` and gate the call behind `#ifdef DRAXUL_ENABLE_RENDER_TESTS` (it already is).
- [x] Update `FakeWindow` in `tests/support/fake_window.h` — inherits new virtuals from IWindow defaults (no overrides needed).

### Phase 2: Change App to hold IWindow by pointer

- [x] Change `App` member from `SdlWindow window_` to `std::unique_ptr<IWindow> window_`.
- [x] In the default (production) path, `App::initialize()` creates a `std::make_unique<SdlWindow>()` and stores it. The `set_size_logical()` call in `normalize_render_target_window_size` uses a `dynamic_cast<SdlWindow*>` since it's render-test-only.
- [x] Add `window_factory` to `AppOptions`: `std::function<std::unique_ptr<IWindow>()>`. If set, App calls it instead of creating `SdlWindow`. Replaces the old `window_init_fn`.
- [x] Changed `InputDispatcher::connect(SdlWindow&)` to `connect(IWindow&)`.
- [x] Updated 3 existing test files to use `window_factory` instead of `window_init_fn`.

### Phase 3: App unit tests

With the above in place, tests can construct an `App` with fake window + fake renderer that need no GPU:

- [x] **initialization rollback tests**: Renderer failure (empty bundle), null window, font failure — all return false and shutdown cleanly.
- [x] **dead host test**: Nonexistent host binary causes init failure; shutdown is safe.
- [x] **double shutdown test**: Double shutdown after failed init is safe.
- [x] **fake renderer interface test**: FakeTermRenderer begin_frame/end_frame matches render_frame() needs.

---

## Acceptance

- `App` holds `IWindow` by pointer, not `SdlWindow` by value.
- All 6 SdlWindow-only methods used by App are on `IWindow` (with sensible defaults).
- At least 3 new App-level unit tests pass using `FakeWindow` + `FakeTermRenderer`.
- Existing smoke test and render tests still pass.
- No production behavior change.

---

## Interdependencies

- Supersedes work item 04 (app-smoke-test) — that item identified the problem; this item solves it.
- Benefits from the pump_once decomposition (work item 11, now complete) — the extracted methods are the natural test targets.

---

*claude-opus-4-6*
