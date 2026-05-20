# 10 renderer-shutdown-idempotency -test

**Priority:** MEDIUM
**Type:** Test
**Raised by:** Claude
**Model:** claude-sonnet-4-6

---

## Problem

`shutdown()` is never validated for idempotency or full resource release in either `VkRenderer` or `MetalRenderer`. Calling `shutdown()` twice (which can happen in certain error paths) could cause double-free of GPU resources, Vulkan validation layer errors, or ObjC++ over-release.

---

## Implementation Plan

- [x] Read `libs/draxul-renderer/src/vulkan/vk_renderer.cpp:166–189` and the Metal equivalent for `shutdown()` paths.
- [x] Determine whether a headless/no-GPU test can call `shutdown()` — if a real GPU is required, this test must be gated appropriately (e.g., only on machines with the right driver, or skipped in headless CI).
- [x] If a fake/null renderer can be constructed without a real GPU:
  - Write `tests/renderer_shutdown_tests.cpp`.
  - Instantiate the renderer in a stub/null configuration.
  - Call `shutdown()`.
  - Call `shutdown()` again.
  - Assert: no crash, no ASAN error, no Vulkan validation callback fired.
- [x] If a real GPU is required, at minimum add documentation to `tests/to-be-checked.md` with a manual test procedure, and create a placeholder test that `SKIP()` in headless CI.
- [x] Add to `tests/CMakeLists.txt`.
- [ ] Run on both macOS (Metal) and Windows (Vulkan) if possible. (Real-GPU test body is a documented stub behind `DRAXUL_TEST_REAL_RENDERER=1` env var.)

---

## Acceptance

- Double-`shutdown()` on both renderer backends produces no crash, no double-free, no Vulkan validation error.
- Single `shutdown()` releases all GPU resources (verify via ASAN/memory tracking where possible).

---

## Interdependencies

- Potentially blocked by the absence of a **fake renderer** for headless tests (noted as a gap by Claude: "No Fake Renderer for Headless Unit Tests"). If creating the fake renderer is the chosen approach, that becomes a dependency.
- FakeTermRenderer now exercises the double-shutdown contract; real-GPU test is SKIP-gated.
