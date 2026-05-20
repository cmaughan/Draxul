# WI 48 — vk-null-grid-handle-dereference

**Type**: bug  
**Priority**: 0 (crash — highest)  
**Source**: review-consensus.md §B1 [P]  
**Produced by**: claude-sonnet-4-6

---

## Problem

`VkRenderer::create_grid_handle()` (`libs/draxul-renderer/src/vulkan/vk_renderer.cpp:612`) can return `nullptr` when descriptor-set allocation fails. Two call sites dereference the result unconditionally:

1. `GridHostBase::initialize()` — `libs/draxul-host/src/grid_host_base.cpp:14`
2. `CommandPaletteHost::dispatch_action()` — `app/command_palette_host.cpp:118`

A transient GPU resource hiccup at startup or palette-open becomes a hard crash (null dereference).

The Metal backend `MetalRenderer::create_grid_handle()` should be audited for the same pattern.

---

## Tasks

- [ ] Read `libs/draxul-renderer/src/vulkan/vk_renderer.cpp` around `create_grid_handle()` and confirm the null return path.
- [ ] Read `libs/draxul-host/src/grid_host_base.cpp` — find all dereferences of the returned handle and add null-check + error return.
- [ ] Read `app/command_palette_host.cpp` — find the `dispatch_action()` dereference and add null-check; propagate failure as a log + no-op or return false.
- [ ] Audit `libs/draxul-renderer/src/metal/metal_renderer.mm` `create_grid_handle()` for the same null-return pattern.
- [ ] Audit any other call sites of `create_grid_handle()` in the codebase for unconditional dereference.
- [ ] Build: `cmake --build build --target draxul draxul-tests`
- [ ] Run smoke test: `py do.py smoke`

---

## Acceptance Criteria

- `GridHostBase::initialize()` returns a failure result (not crash) if `create_grid_handle()` returns null.
- `CommandPaletteHost::dispatch_action()` logs an error and returns early (not crash) if the handle is null.
- All other call sites checked and safe.
- Smoke test passes.

---

## Interdependencies

- **WI 54** (grid-handle-null-init-test) — regression test for this fix; file both in the same agent pass.

---

## Notes for Agent

- Do not change the public `IGridRenderer::create_grid_handle()` interface signature.
- The fix is purely defensive null-checks at call sites; do not change the renderer internals.
- Use `DRAXUL_LOG_ERROR` for the failure log at the call site.
