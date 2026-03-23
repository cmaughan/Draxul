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

- [ ] Add `wake()`, `activate()`, `wait_events(int timeout_ms) -> bool` as virtual methods on `IWindow` with default no-op/true implementations (so existing `FakeWindow` compiles without changes).
- [ ] Move `set_clamp_to_display()` and `set_hidden()` to `IWindow` with default no-ops (these are init-time config, harmless to ignore in tests).
- [ ] `set_size_logical()` is only used in the render test normalization helper — leave it on `SdlWindow` and gate the call behind `#ifdef DRAXUL_ENABLE_RENDER_TESTS` (it already is).
- [ ] Update `FakeWindow` in `tests/support/fake_window.h` to override the new virtuals. `wait_events()` should return `true` immediately (no blocking). `wake()` and `activate()` are no-ops.

### Phase 2: Change App to hold IWindow by pointer

- [ ] Change `App` member from `SdlWindow window_` to `std::unique_ptr<IWindow> window_`.
- [ ] In the default (production) path, `App::initialize()` creates a `std::make_unique<SdlWindow>()` and stores it. The `set_size_logical()` call in `normalize_render_target_window_size` uses a `dynamic_cast<SdlWindow*>` since it's render-test-only.
- [ ] Add `window_factory` to `AppOptions`: `std::function<std::unique_ptr<IWindow>()>`. If set, App calls it instead of creating `SdlWindow`. This replaces the current `window_init_fn` (which is half-abstract — it returns bool but mutates the concrete member).
- [ ] Similarly, the existing `renderer_create_fn` in `AppOptions` already returns a `RendererBundle` — this path is already testable. No change needed.

### Phase 3: App unit tests

With the above in place, tests can construct an `App` with fake window + fake renderer that need no GPU:

- [ ] **pump loop test**: Construct App with FakeWindow + FakeTermRenderer. Initialize. Call `pump_once()` N times. Verify no crash, `close_dead_panes` correctly detects dead hosts.
- [ ] **initialization rollback test**: Inject a renderer factory that returns an empty `RendererBundle`. Verify `initialize()` returns false and `shutdown()` completes cleanly.
- [ ] **frame request flow test**: Set `frame_requested_ = true` (via `request_frame()`), call `pump_once()`, verify `render_frame()` was exercised (check `FakeTermRenderer::begin_frame` was called).

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
