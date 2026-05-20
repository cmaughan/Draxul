# 37 Expand Placeholders Fragile Path

## Why This Exists

`render_test.cpp` expands `${PROJECT_ROOT}` by walking two levels up from the scenario file's directory:
```cpp
scenario_dir.parent_path().parent_path()
```
If a scenario file is moved, nested differently, or the directory structure changes, this expansion silently produces the wrong path with no error or diagnostic. It is also not tested for Windows path separators or symlinks.

Identified by: **Claude** (smells #4 and worst features #6).

## Goal

Replace the depth-counting heuristic with an explicit project root. The best approach is a CMake-injected `#define PROJECT_ROOT_DIR "..."` compiled into the test binary, or an environment variable fallback.

## Implementation Plan

- [x] Read `app/render_test.cpp` and `app/render_test.h` to understand `expand_placeholders` and `load_render_test_scenario`.
- [x] Read `CMakeLists.txt` and `libs/draxul-app-support/CMakeLists.txt` to find where render tests are compiled.
- [x] Add a CMake `target_compile_definitions` to the test or app-support target:
  ```cmake
  target_compile_definitions(draxul-app-support PRIVATE
      DRAXUL_PROJECT_ROOT="${CMAKE_SOURCE_DIR}")
  ```
- [x] In `render_test.cpp`, replace the `parent_path().parent_path()` heuristic with `std::filesystem::path{DRAXUL_PROJECT_ROOT}`.
- [x] Add a unit test asserting that `expand_placeholders("${PROJECT_ROOT}/foo")` returns the correct absolute path.
- [x] Verify both Windows (backslash normalisation) and macOS paths (handled by `normalized_path_string` via `lexically_normal().generic_string()`).
- [x] Run `ctest` and `clang-format` on touched files.

## Sub-Agent Split

Single agent. Changes are confined to `render_test.cpp` and one CMakeLists.txt.
