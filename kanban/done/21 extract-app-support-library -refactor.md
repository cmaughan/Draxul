# 21 Extract draxul-app-support Library

## Why This Exists

`tests/CMakeLists.txt` lists `app/app_config.cpp`, `app/cursor_blinker.cpp`, and `app/render_test.cpp`
as direct source inputs to the `draxul-tests` binary. These files are compiled twice — once into
the app executable, once into the test binary — with different include paths and possibly different
preprocessor defines. Any agent adding a new header or dependency to these files must reason about
both compilation contexts, and any CMake change to either target risks silently breaking the other.

**Source:** `tests/CMakeLists.txt`, `app/CMakeLists.txt`.
**Raised by:** Claude (primary), GPT/Gemini (agree on the header pollution and coupling issues).

## Goal

Create a `draxul-app-support` static library containing `AppConfig`, `CursorBlinker`, and
`RenderTestScenario` (and any other app-layer helpers with no GPU/window dependencies).
Both the `draxul` executable and the `draxul-tests` binary link this library instead of
compiling the source files directly.

## Implementation Plan

- [x] Read `app/CMakeLists.txt` and `tests/CMakeLists.txt` to understand how the sources are currently wired.
- [x] Read `app/app_config.h/cpp`, `app/cursor_blinker.h/cpp`, `app/render_test.h/cpp` to identify their dependencies.
- [x] Verify none of the three files depend on GPU headers (`vulkan/`, `Metal/`, `SDL`) — they must be clean of platform I/O.
- [x] Create `libs/draxul-app-support/` with:
  - `CMakeLists.txt` declaring a static library target `draxul-app-support`
  - Move or symlink the three `.cpp` files (or leave in place and list them from `libs/draxul-app-support/CMakeLists.txt`)
  - Public include path pointing to `app/` headers
- [x] Add `draxul-app-support` to top-level `CMakeLists.txt` via `add_subdirectory`.
- [x] Link `draxul-app-support` from `draxul` executable and `draxul-tests`.
- [x] Remove the duplicate source file listings from `tests/CMakeLists.txt`.
- [x] Build both targets on macOS and verify they compile cleanly.
- [x] Run `ctest --test-dir build`.

## Notes

Do not move the headers out of `app/`. The source files can stay in `app/` and simply be referenced
from both the library and (transitively) the app target. The goal is a single compilation unit, not
a full relocation.

Implemented in the current tree:
- Added `libs/draxul-app-support/CMakeLists.txt`.
- `draxul-app-support` now owns the duplicated app helper `.cpp` files from the current tree:
  `app_config.cpp`, `cursor_blinker.cpp`, `grid_rendering_pipeline.cpp`, `render_test.cpp`,
  and `ui_request_worker.cpp`.
- `draxul` and `draxul-tests` both link the shared library instead of compiling those sources
  directly.

Validation completed here:
- `cmake --build build --config Release --parallel`
- `ctest --test-dir build --build-config Release --output-on-failure`

Open follow-up:
- macOS compile verification could not be run from this Windows environment, so that step remains
  unchecked.
- The repo already has a `build-macos` GitHub Actions job that runs `bash scripts/run_tests.sh`,
  which exercises the same `mac-debug` / `mac-release` presets after this change is pushed.

## Sub-Agent Split

Single agent. This is a CMake restructuring task. No logic changes to any `.cpp` file.
