# WI 56 — library-boundary-renderer-includes

**Type**: refactor  
**Priority**: 8 (library boundary porousness makes private renderer refactors expensive)  
**Source**: review-consensus.md §R2 [P][C][G]  
**Produced by**: claude-sonnet-4-6

---

## Problem

Two library boundary violations exist that expose renderer-private headers to consumers who should only see the public API:

1. **`draxul-ui`**: `libs/draxul-ui/src/ui_panel.cpp` reaches into renderer headers via a relative path. The `draxul-ui` CMakeLists.txt does not declare a dependency on `draxul-renderer`, making this a hidden link-order dependency.

2. **`draxul-megacity`**: `libs/draxul-megacity/CMakeLists.txt:80` explicitly adds renderer-private backend directories to the include path. `megacity_render_vk.cpp` and `megacity_render.mm` include private render-context headers directly. Any refactor of private renderer internals requires coordinated changes in MegaCity.

These violations mean:
- A private renderer header rename silently breaks a dependent library.
- CI does not catch missing `target_link_libraries` because the relative-path include bypasses CMake's dependency tracking.

---

## Tasks

- [x] Read `libs/draxul-ui/src/ui_panel.cpp` — included `../../draxul-renderer/include/draxul/base_renderer.h` via relative path to reach `IFrameContext::render_imgui()`.
- [x] Read `libs/draxul-ui/CMakeLists.txt` — `draxul-renderer` was not in `target_link_libraries`.
- [x] Determine whether the `draxul-ui` dependency on the renderer is necessary — `IFrameContext::render_imgui()` is called via the full member function so a forward declaration is insufficient; `draxul-renderer` is now an explicit PRIVATE dep. To break the resulting cycle (`draxul-renderer` previously depended on `draxul-ui` via `<draxul/imgui_host.h>`), `imgui_host.h` was moved into `draxul-renderer/include/draxul/` since the concrete renderers (`MetalRenderer`, `VkRenderer`) are the things that satisfy `IImGuiHost`.
- [x] Replace the relative-path include in `ui_panel.cpp` with `#include <draxul/base_renderer.h>`.
- [x] Read `libs/draxul-megacity/CMakeLists.txt` around line 80 — identified `${CMAKE_SOURCE_DIR}/libs/draxul-renderer/src/metal` and `.../src/vulkan` private include paths.
- [x] Read `libs/draxul-megacity/src/megacity_render_vk.cpp` and `megacity_render.mm` — they pulled `vk_render_context.h`, `metal_render_context.h`, and `objc_ref.h` from those private dirs.
- [x] For each private header used by MegaCity: `metal_render_context.h`, `vk_render_context.h`, and `objc_ref.h` were promoted to `libs/draxul-renderer/include/draxul/{metal,vulkan}/`. They are inherently part of any IRenderPass implementer's contract on the respective backend, so this matches their actual public role.
- [x] Remove the private-directory `include_directories` from `libs/draxul-megacity/CMakeLists.txt`.
- [x] Also removed the equivalent `libs/draxul-nanovg/CMakeLists.txt` private include hack (same headers).
- [x] Full build both platforms or at minimum the active platform: macOS `draxul`/`draxul-tests` build clean.
- [x] Run smoke test: passes.

### Deferred (separate follow-up)

- The `tests/CMakeLists.txt` `${CMAKE_SOURCE_DIR}/libs/draxul-megacity/src` include used by megacity scene/host/config tests is **not** addressed by this item. Removing it requires promoting ~10 megacity scene headers to public API or building a megacity test-support helper library, which is its own design problem. WI 74's deferred test→megacity item retains this scope.

---

## Acceptance Criteria

- `draxul-ui` CMakeLists.txt declares its actual dependencies; no relative-path renderer includes remain.
- `draxul-megacity` CMakeLists.txt does not add renderer-private directories to the include path.
- All existing tests pass.
- Smoke test passes.

---

## Interdependencies

- Independent of WI 48–55 and WI 57–60.
- **High blast radius**: touches CMakeLists across two libraries and their source files. Full build verification required.
- A sub-agent is appropriate here due to the breadth of files to read before making changes.

---

## Notes for Agent

- Read all relevant files first; do not guess which headers are included.
- Prefer promoting types to the public interface over copying headers.
- If promoting a private renderer type requires significant API design work, scope the fix to `draxul-ui` only and file a separate item for `draxul-megacity`'s renderer coupling.
- After the fix, verify with `cmake --build build 2>&1 | grep -i "warning\|error"` that no new include-path warnings appear.
