# 17 megacity-optional-cmake-flag — Refactor

## Summary

`draxul-megacity` (the spinning-cube 3D demo host) is compiled and linked unconditionally into every Draxul build. This adds:

- Compile time for every build, including CI builds that only care about terminal correctness.
- Binary size (the megacity library and its shaders are always included).
- Interface complexity: `IRenderer` or `RendererBundle` must expose `I3DPassProvider` / `threed()` APIs even for users who never run the megacity host.

Adding a `DRAXUL_ENABLE_MEGACITY` CMake option (defaulting to `ON` to avoid breaking existing builds) makes megacity opt-out without changing the default experience.

**Long-term significance:** Once this flag exists, the unconditional `dynamic_cast` or `threed()` accessor in the renderer can be guarded or removed when `DRAXUL_ENABLE_MEGACITY=OFF`, eventually allowing a cleaner `IRenderer` interface.

## Steps

- [x] 1. Read the top-level `CMakeLists.txt` in full to find where `draxul-megacity` (or the megacity library) is added as a dependency or linked.
- [x] 2. Read `libs/draxul-megacity/CMakeLists.txt` (if it exists) to understand the library structure and any shaders/assets it compiles.
- [x] 3. Read `app/renderer_factory.cpp` and `app/app.cpp` for any `#include <draxul/megacity_host.h>` or `megacity` symbol references.
- [x] 4. Add the CMake option in the top-level `CMakeLists.txt`, near other option declarations.
- [x] 5. Wrap the megacity library's `add_subdirectory` call in `if(DRAXUL_ENABLE_MEGACITY)`.
- [x] 6. Wrap the `target_link_libraries(draxul … draxul-megacity …)` line in `if(DRAXUL_ENABLE_MEGACITY)`.
- [x] 7. In `app/host_manager.cpp`, guard the megacity include and host creation with `#ifdef DRAXUL_ENABLE_MEGACITY`.
- [x] 8. Add `target_compile_definitions` propagation in CMakeLists.txt.
- [x] 9. Test with megacity ENABLED (default): build succeeds.
- [x] 10. Test with megacity DISABLED: build succeeds with no megacity symbols referenced.
- [x] 11. Run the test suite with megacity disabled: all tests pass.
- [x] 12. Run the smoke test: `py do.py smoke` (with default/enabled megacity).
- [x] 13. Add a comment near the `option()` line explaining the long-term intent (optional I3DPassProvider cleanup).
- [x] 14. Mark this work item complete and move to `plans/work-items-complete/`.

## Acceptance Criteria

- `cmake -DDRAXUL_ENABLE_MEGACITY=OFF` builds successfully with no megacity symbols.
- The default build (`cmake --preset mac-debug` without the flag) still includes megacity.
- The `DRAXUL_ENABLE_MEGACITY` preprocessor define is propagated to C++ compilation.
- All tests pass in both configurations.
- Smoke test passes in the default (enabled) configuration.

*Authored by: claude-sonnet-4-6*
