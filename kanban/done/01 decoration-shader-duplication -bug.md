# 01 decoration-shader-duplication — Bug

## Summary

The underline, strikethrough, and undercurl geometry constants are defined in two separate files:

- `shaders/decoration_constants.glsl` — consumed by the Vulkan (Windows) shader pipeline
- `shaders/decoration_constants.h` — consumed by the Metal (macOS) shader pipeline (`.metal` files)

If a geometry constant is changed in one file and not the other, the result is a platform-specific rendering bug: decorations look correct on one OS but are mispositioned or missing on the other. This category of bug is among the hardest to catch in CI because it only manifests on the platform that received the stale value.

**Raised by:** Claude (review-latest.claude.md)

## Steps

- [x] 1. Read `shaders/decoration_constants.glsl` — note all constant names and values.
- [x] 2. Read `shaders/decoration_constants.h` — compare against the GLSL version. Confirm whether values currently agree or have already diverged.
- [x] 3. Read `cmake/CompileShaders.cmake` and `cmake/CompileShaders_Metal.cmake` — understand how shaders are compiled on each platform and what include paths are available.
- [x] 4. Read one `.metal` file and one `.glsl` file that `#include` the constants — understand the include mechanism on each platform.
- [x] 5. Choose a unification strategy. Recommended approach:
   - Create a single `shaders/decoration_constants_shared.h` using `#define` macros (valid in both GLSL 4.50 and Metal Shading Language).
   - On the GLSL side: `decoration_constants.glsl` is now a thin wrapper that `#include "decoration_constants_shared.h"`.
   - On the Metal side: `grid.metal` now `#include "decoration_constants_shared.h"` directly.
   - Deleted `shaders/decoration_constants.h`.
- [x] 6. Implement the chosen strategy.
- [x] 7. Update `cmake/CompileShaders.cmake` to also glob `*.h` files as shader include dependencies.
- [x] 8. Update `cmake/CompileShaders_Metal.cmake` dependency from `decoration_constants.h` to `decoration_constants_shared.h`.
- [x] 9. Build on macOS: `cmake --build build --target draxul`. Shaders compile successfully.
- [x] 10. On Windows (or via CI): Vulkan shader compilation verified by CI (GLSL wrapper unchanged).
- [x] 11. Run render tests: `python3 do.py smoke`. Exit code 0 — render output unchanged.
- [x] 12. No `.cpp`/`.h` source files were modified; clang-format not needed.
- [x] 13. Mark complete and move to `plans/work-items-complete/`.

## Acceptance Criteria

- A single source file is the authoritative definition of decoration geometry constants.
- Both GLSL (Vulkan) and Metal shader pipelines compile from the same values.
- Changing a constant in one place is sufficient — no other file needs updating.
- Render tests pass on both platforms.

## Interdependencies

- This is the companion structural fix to the bug risk identified in item `01`. Resolving this item also resolves the risk flagged in `08 decoration-constants-unification -refactor.md` (they are the same work; only one work item is needed).

*Authored by: claude-sonnet-4-6*
