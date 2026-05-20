# WI 01 — metal-renderer-init-unchecked

**Type:** bug  
**Priority:** HIGH (renderer starts in broken state on startup failure; later frames crash or silently fail)  
**Platform:** macOS only  
**Source:** review-bugs-consensus.md — BUG-02 (GPT)

---

## Problem

`MetalRenderer::initialize()` in `libs/draxul-renderer/src/metal/metal_renderer.mm` creates four objects that are mandatory for all rendering:
- `command_queue_` at line 285
- `atlas_texture_` at line 388
- `atlas_sampler_` at line 399
- `frame_semaphore_` at line 403

None of them are null-checked before `return true;` at line 406. Under resource pressure, device limits, or headless-Metal CI without a real GPU, any of these can be `nil`/`NULL`. The caller receives `true` and proceeds into the render loop, where the first access to a null resource either crashes or silently no-ops with no diagnostic.

---

## Investigation

- [x] Read `libs/draxul-renderer/src/metal/metal_renderer.mm` lines 278–410 to confirm which allocations lack nil checks.
- [x] Check all other `return true` and `return false` paths in `initialize()` to understand existing error handling.
- [x] Identify callers of `initialize()` (likely in `renderer_factory.cpp` or `app.cpp`) to see how they handle `false`.

---

## Fix Strategy

- [x] After each mandatory allocation, add a null check with an error log and `return false`:
  ```cpp
  command_queue_.reset([device newCommandQueue]);
  if (!command_queue_) {
      DRAXUL_LOG_ERROR(LogCategory::Renderer, "MetalRenderer::initialize: newCommandQueue returned nil");
      return false;
  }
  ```
  Apply the same pattern for `atlas_texture_`, `atlas_sampler_`, and `frame_semaphore_`.
- [ ] Coordinate with WI 00 (BUG-01) and WI 02 (BUG-03) — fix all three Metal allocation issues in one PR.

---

## Acceptance Criteria

- [x] `initialize()` returns `false` (with an error log) if any of the four mandatory objects is nil.
- [x] The renderer never enters a partially-initialized broken state.
- [x] Build and smoke test pass: `cmake --build build --target draxul draxul-tests && py do.py smoke`.
- [ ] No new ASan findings.
