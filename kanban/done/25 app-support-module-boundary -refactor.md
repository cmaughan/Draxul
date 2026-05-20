# App-Support Module Boundary Fix

**Type:** refactor
**Priority:** 25
**Raised by:** GPT, Gemini

## Summary

`libs/draxul-app-support/CMakeLists.txt` currently compiles `app/*.cpp` source files directly into the library. This inverts the intended dependency direction (libraries should not reach into the app directory) and prevents the library from being tested in isolation. Extract the shared configuration and helper logic into proper translation units inside `draxul-app-support/src/`.

## Background

The dependency graph in `CLAUDE.md` shows `draxul-app-support` sitting between `draxul-nvim` and the `app` executable. The current build wiring breaks this: the library directly compiles app-layer source files. This means the library cannot be built or tested without the full app build tree, tests cannot link against `draxul-app-support` without dragging in app-specific dependencies, and the module boundary is effectively non-existent. Both GPT and Gemini flagged this as a structural issue.

## Implementation Plan

### Files to modify
- `libs/draxul-app-support/CMakeLists.txt` — remove direct compilation of `app/*.cpp` files; add proper `target_sources` for new files in `libs/draxul-app-support/src/`
- `libs/draxul-app-support/src/` — create or move shared helpers here (config loading, `AppConfig`, any utilities shared between app and tests)
- `app/CMakeLists.txt` — ensure `app/*.cpp` files remain compiled only by the app target, not by the library
- `libs/draxul-app-support/include/draxul/` — update public headers to expose the extracted API

### Steps
- [x] Identify which `app/*.cpp` files are currently compiled by `draxul-app-support` and why
- [x] For each such file, determine whether the logic belongs in the library (shared config, AppConfig struct, font settings, keybinding config) or in the app (SDL event loop, renderer lifecycle)
- [x] Move library-appropriate code to new files in `libs/draxul-app-support/src/`
- [x] Update `libs/draxul-app-support/CMakeLists.txt` to compile only files in its own `src/` directory
- [x] Update `app/CMakeLists.txt` to compile the app-only files
- [x] Verify `cmake --build` succeeds for both Windows and macOS presets
- [x] Run `ctest` to confirm nothing is broken

## Depends On
- None

## Blocks
- `31 app-class-decomposition -refactor.md` — cleaner module boundaries make the App decomposition easier

## Notes
This refactor may require updating `#include` paths in files that previously reached across the boundary via the leaked include paths. Check for any `../../app/` relative includes in test files.

> Work item produced by: claude-sonnet-4-6
