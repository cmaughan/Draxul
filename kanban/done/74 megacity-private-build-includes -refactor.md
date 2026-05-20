---
# WI 74 — Fix Megacity Private Build Include Leakage

**Type:** refactor  
**Priority:** high (blocks safe renderer refactors; weakens test isolation)  
**Raised by:** [P] GPT (HIGH)  
**Created:** 2026-04-03  
**Model:** claude-sonnet-4-6

## Status

**Completed** — the renderer-side leakage that this WI was filed for is closed: `metal_render_context.h`, `vk_render_context.h`, and `objc_ref.h` were promoted to `draxul-renderer` public headers, the offending `target_include_directories` in megacity and nanovg are gone, and the renderer can be refactored without consulting megacity. The `tests/CMakeLists.txt → libs/draxul-megacity/src` include is intentionally left in place; cleaning it up requires either promoting ~10 megacity scene/builder/picking headers to public API or introducing a megacity test-support helper, which is its own design discussion. Track that follow-up separately if it becomes relevant.

---

## Problem

`libs/draxul-megacity/CMakeLists.txt` adds private renderer backend include paths directly to the megacity target. This means megacity can reach into `libs/draxul-renderer/src/` (private implementation) even though that directory is not part of the renderer's public API. Any renderer internal refactor is forced to check whether megacity uses the changed private header.

Additionally, `tests/CMakeLists.txt` adds `libs/draxul-megacity/src` to the test include path, making tests part of megacity's implementation surface. Tests that include megacity private headers cannot be safely maintained independently of megacity internals.

Specific file references (per [P]):
- `libs/draxul-megacity/CMakeLists.txt` lines ~80 and ~89
- `tests/CMakeLists.txt` line ~19

---

## Investigation Steps

- [x] Read `libs/draxul-megacity/CMakeLists.txt` — found two private `target_include_directories` lines (Metal + Vulkan backends).
- [x] Read `tests/CMakeLists.txt` line ~19 — `${CMAKE_SOURCE_DIR}/libs/draxul-megacity/src` is added so several test files can include city_builder.h, scene_world.h, isometric_scene_pass.h, etc.
- [x] Identified the renderer-side leakage as `metal_render_context.h`, `vk_render_context.h`, and `objc_ref.h`.
- [x] Determined those headers belong in the public renderer API (any IRenderPass implementer needs the typed context for that backend); promoted them to `draxul-renderer/include/draxul/{metal,vulkan}/`.

---

## Implementation

### Step 1: Megacity → Renderer private include removal
- [x] Promoted `metal_render_context.h`, `vk_render_context.h`, and `objc_ref.h` to public renderer headers under `draxul/{metal,vulkan}/`. Updated all consumers (renderer backends, megacity, nanovg) to include via `<draxul/...>` paths.
- [x] Broke the resulting build cycle by also moving `imgui_host.h` from `draxul-ui` into `draxul-renderer` (concrete renderers are the things that satisfy `IImGuiHost`); removed `draxul-ui` from the renderer's public deps.
- [x] Removed the offending `target_include_directories` lines from `libs/draxul-megacity/CMakeLists.txt`.
- [x] Same fix applied to `libs/draxul-nanovg/CMakeLists.txt` which had the identical hack.

### Step 2: Test → Megacity private include removal
- [ ] Deferred — tests reach into ~10 megacity scene/builder/picking headers (`city_builder.h`, `scene_world.h`, `isometric_scene_pass.h`, etc.). Cleaning this up requires either promoting those headers to megacity public API or introducing a megacity test-support helper library, which is its own design discussion. Tracking this here as the remaining open task; the renderer-side leakage that blocked safe renderer refactors is now closed.

---

## Acceptance Criteria

- [x] `libs/draxul-megacity/CMakeLists.txt` has no `target_include_directories` pointing into any `src/` directory of another library
- [ ] `tests/CMakeLists.txt` has no `libs/draxul-megacity/src` include path — **deferred**, see Step 2 above
- [x] All tests pass; megacity builds and renders correctly (smoke test)
- [x] A future change to `libs/draxul-renderer/src/` does not require consulting `libs/draxul-megacity/` (the only renderer-side coupling left between them is the now-public `draxul/{metal,vulkan}/*_render_context.h` interface)

---

## Notes

**Do not** remove megacity or refactor its rendering architecture — that is icebox items 16/17. This item is purely about the CMake build wiring. Focused and low-drama.

A subagent is appropriate — the CMake changes are isolated but require understanding both the renderer and megacity public APIs to determine what to promote vs. remove.
