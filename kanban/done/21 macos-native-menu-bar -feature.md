# 21 macos-native-menu-bar -feature

**Priority:** LOW (feature, quality of life)
**Type:** Feature
**Raised by:** Claude ("macOS native menu bar integration")
**Model:** claude-sonnet-4-6

---

## Problem

Draxul on macOS has no native menu bar. Users cannot access common actions (new window, open file, copy/paste, font size, quit) via the macOS application menu. This makes Draxul feel like a non-native application and breaks muscle memory for users accustomed to macOS conventions. Apps without a menu bar also fail some macOS accessibility and App Store requirements.

This is macOS-only. The Windows equivalent (system menu / tray) is a separate concern.

---

## Implementation Plan

- [ ] Read `app/main.cpp`, `app/app.cpp`, and `libs/draxul-window/src/sdl_window.cpp` to understand how the macOS application lifecycle is currently managed.
- [ ] Determine how SDL3 interacts with the macOS menu bar (SDL3 may create a minimal default menu; investigate `SDL_SetAppMetadata` and related APIs).
- [ ] Create `app/macos_menu.mm` (Objective-C++) to build a native `NSMenu` hierarchy:
  - **App menu**: About Draxul, Preferences (→ opens config.toml or future Config GUI), Quit
  - **File menu**: Open File (→ dispatch `open_file` action with dialog), Close Window
  - **Edit menu**: Copy (→ dispatch `copy`), Paste (→ dispatch `paste`)
  - **View menu**: Toggle Diagnostics (→ dispatch `toggle_diagnostics`), Increase Font (→ `font_increase`), Decrease Font (→ `font_decrease`), Reset Font (→ `font_reset`)
- [ ] Wire menu item actions to `GuiActionHandler` / `dispatch_action()` (or the typed map successor).
- [ ] Ensure the menu bar is only compiled on macOS (`#ifdef __APPLE__` / CMake platform guard).
- [ ] Guard `app/CMakeLists.txt` to include `macos_menu.mm` only on Apple.
- [ ] Build with `cmake --preset mac-debug` and verify the menu appears and dispatches actions.
- [ ] Run smoke test.

---

## Acceptance

- macOS Draxul shows a standard menu bar with App, File, Edit, View menus.
- Each menu item dispatches the correct action.
- No regressions on Windows build (menu code is gated).

---

## Interdependencies

- No hard code dependencies on other items in this wave.
- Would benefit from **15-refactor** (AppConfig SDL decoupling) being done first if menu triggers config operations.
- If the icebox `command-palette` (60) is ever implemented, some menu items could delegate to it.

---

## Note on Sub-agent Suitability

This feature is self-contained (new `.mm` file + CMake wiring). A sub-agent can implement it independently with read access to `app/` and `libs/draxul-window/`. No other concurrent work item touches `macos_menu.mm`.
