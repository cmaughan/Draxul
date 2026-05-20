# 59 Missing Nvim Error Dialog

## Why This Exists

If `nvim` is not on PATH, `App::initialize()` returns false and the process exits silently. Users see nothing — no dialog, no console message. This is one of the most common setup problems for new users, and the silent exit is interpreted as a crash.

Identified by: **Claude** (worst features #7), **Gemini** (implied by lifecycle tests).

## Goal

When nvim is not found or fails to start, display a platform-native error dialog with a clear message ("nvim not found on PATH — please install Neovim and ensure it is in your PATH") before exiting.

## Implementation Plan

- [x] Read `app/main.cpp` and `app/app.cpp` for `App::initialize()` and its failure path.
- [x] Read `libs/draxul-window/` for any existing dialog or message box API.
- [x] On macOS: use `SDL_ShowSimpleMessageBox`.
- [x] On Windows: use `SDL_ShowSimpleMessageBox`.
- [x] Prefer `SDL_ShowSimpleMessageBox` as it is cross-platform and already linked.
- [x] In `main.cpp`, after `app.initialize()` returns false, call `SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Draxul", message, nullptr)` before exit.
- [x] Detect the specific error (nvim not found vs. nvim crashed on startup) and provide distinct messages.
- [x] Suppress the dialog when `--smoke-test` or `--render-test` is active (CI must not hang on a dialog).
- [x] Run `ctest` and `clang-format`.

## Sub-Agent Split

Single agent. Confined to `main.cpp` and `app.cpp` initialisation path.
