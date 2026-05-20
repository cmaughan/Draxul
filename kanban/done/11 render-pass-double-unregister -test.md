# 11 render-pass-double-unregister -test

**Priority:** MEDIUM
**Type:** Test (safety guard for render pass lifecycle)
**Raised by:** Claude
**Model:** claude-sonnet-4-6

---

## Problem

`I3DRenderer::unregister_render_pass()` takes no argument and removes the single registered render pass. There is no guard against calling it twice (or when no pass is registered). `MegaCityHost::detach_3d_renderer()` could trigger a double-unregister in a rapid attach/detach cycle. The current lack of test means a crash or silent state corruption would go unnoticed.

---

## Code Locations

- `libs/draxul-renderer/include/draxul/base_renderer.h:83` — `unregister_render_pass()` declaration
- Metal and Vulkan renderer implementations — where the registered pass pointer is stored
- `libs/draxul-megacity/` — `MegaCityHost::detach_3d_renderer()` call site
- `tests/` — test file to be added

---

## Implementation Plan

- [x] Read `base_renderer.h` and the renderer implementation (Metal or Vulkan) to see how `register_render_pass()` and `unregister_render_pass()` are implemented.
- [x] Identify the fake/stub renderer used in existing tests (likely a `FakeRenderer` or similar in `tests/`).
- [x] If a fake renderer exists, use it. Otherwise create a minimal stub that implements `I3DRenderer` with a `shared_ptr<IRenderPass>` slot.
- [x] Write a test:
  - Register a pass. Verify it is stored.
  - Call `unregister_render_pass()`. Verify the slot is cleared.
  - Call `unregister_render_pass()` again. Assert no crash, no exception, no double-free.
- [x] Write a second test: call `unregister_render_pass()` without ever registering a pass. Assert no crash.
- [x] Build and run tests.
- [x] Run `clang-format`.

---

## Acceptance Criteria

- Double-unregister is a safe no-op.
- Unregister-without-register is a safe no-op.
- No regression in existing renderer tests.

---

## Interdependencies

- No upstream blockers. If the implementation is not safe, a one-line null check in `unregister_render_pass()` is the minimal fix — do it alongside writing the test.

---

*claude-sonnet-4-6*
