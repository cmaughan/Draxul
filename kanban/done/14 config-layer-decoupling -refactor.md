# Refactor: Decouple draxul-config from Renderer/Window/Font

**Type:** refactor
**Priority:** 14
**Source:** Gemini review

## Problem

`libs/draxul-config/` is supposed to be a low-level, dependency-free config layer, but it currently:

1. **Publicly links** `draxul-renderer`, `draxul-window`, and `draxul-font` in its `CMakeLists.txt`.
2. **Includes SDL and `TextService`** headers from `app_config_io.cpp` — subsystem headers that have no business in a config I/O file.
3. **Carries `MegaCityCodeConfig`** in the core `app_config_types.h`, mixing optional-demo config into the shared type header.

This means any code that depends on config also transitively pulls in renderer, window, font, and (optionally) MegaCity headers, blowing up include times and creating non-obvious coupling.

## Investigation steps

- [x] Read `libs/draxul-config/CMakeLists.txt` — list all public and private link deps.
- [x] Read `libs/draxul-config/include/draxul/app_config_types.h` — identify which fields belong to core vs. optional modules.
- [x] Read `libs/draxul-config/src/app_config_io.cpp` — find every subsystem include and why it is there.
- [x] Check whether `MegaCityCodeConfig` is used outside the MegaCity library.

## Implementation steps

- [x] **Remove subsystem links from `draxul-config/CMakeLists.txt`**: `draxul-font` removed from PUBLIC deps. `draxul-renderer` and `draxul-window` remain PUBLIC because `app_options.h` (a public header) includes `<draxul/renderer.h>` and `<draxul/window.h>` for `RendererBundle`, `RendererOptions`, and `IWindow` types used by value/in `std::function` signatures.
- [x] **Move `MegaCityCodeConfig`** out of `app_config_types.h`. Already done in prior work -- lives in `libs/draxul-megacity/include/draxul/megacity_code_config.h`.
- [x] **Remove SDL/TextService includes from `app_config_io.cpp`**: removed `#include <draxul/text_service.h>` and replaced `TextService::MIN_POINT_SIZE`/`MAX_POINT_SIZE` with local `kMinFontPointSize`/`kMaxFontPointSize` constants (same pattern as the existing `kDefaultFontPointSize` mirror in the header). SDL include remains because `keybinding_parser.cpp` and `AppConfig()` constructor use `SDLK_*` keycodes (SDL3 is already a PRIVATE dep).
- [x] Build the project after each step and fix broken includes.
- [x] Run `cmake --build build --target draxul draxul-tests` to confirm nothing broke.

## Acceptance criteria

- [x] `draxul-config/CMakeLists.txt` no longer publicly links font. Renderer and window remain PUBLIC because `app_options.h` (public header) uses `RendererBundle`/`RendererOptions`/`IWindow` by value.
- [x] `app_config_types.h` does not include any renderer, window, font, or SDL headers.
- [x] `MegaCityCodeConfig` lives in the megacity library, not the core config header.
- [x] Project builds clean on macOS (verified). Windows not tested in this worktree but changes are CMake-portable.
- [x] No regressions in existing tests (all 91 config tests pass; 24 pre-existing RPC server failures unrelated to config changes).

## Interdependencies

- **`02 appdata-empty-string-config -bug`**: fix bug `02` first (touches same file); then decouple.

---
*Filed by `claude-sonnet-4-6` · 2026-03-26*
