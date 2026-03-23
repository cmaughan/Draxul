# 12 app-config-monolith-split -refactor

**Priority:** MEDIUM
**Type:** Refactor (compile time, module coupling)
**Raised by:** Claude
**Model:** claude-sonnet-4-6

---

## Problem

`app_config.h` is a ~400-line monolith that combines:
- Config struct definitions (`AppConfig`, `AppConfigOverrides`, `GuiKeybinding`)
- TOML parse logic (inline in the header)
- File I/O (`load()`, `save()`)
- Override merge logic
- Chord parsing for keybindings

Every dependent file recompiles whenever any config field changes, even if it only needs the struct layout. The chord parsing and TOML logic cannot be unit-tested without pulling in the full header. This also blocks a clean implementation of the icebox live-config-reload (56) and hierarchical-config (37) items.

---

## Implementation Plan

- [ ] Read `app/app_config.h` and `app/app_config.cpp` (if it exists) to understand the current structure.
- [ ] Identify the natural split boundaries:
  - `app_config_types.h` — struct definitions only (`AppConfig`, `AppConfigOverrides`, `GuiKeybinding`, enums). No parse logic.
  - `app_config_io.h/.cpp` — `load()`, `save()`, TOML parse, override merge. Includes `app_config_types.h` and the TOML library.
  - `keybinding_parser.h/.cpp` — chord parsing for `GuiKeybinding`. Can be tested independently.
- [ ] Perform the split:
  - [ ] Create `app_config_types.h` with structs only.
  - [ ] Move I/O and parse logic to `app_config_io.cpp`.
  - [ ] Move chord parsing to `keybinding_parser.cpp`.
  - [ ] Update all `#include` references across `app/` and `tests/`.
- [ ] Update `CMakeLists.txt` to include the new `.cpp` files.
- [ ] Build and run: `cmake --build build --target draxul draxul-tests && py do.py smoke`.
- [ ] Run `clang-format` on all modified files.

---

## Acceptance

- `app_config_types.h` has no TOML or I/O includes.
- `keybinding_parser.cpp` can be tested without including the full config I/O machinery.
- All tests pass; no behavior change.

---

## Interdependencies

- **Unblocks** icebox items: live-config-reload (56), hierarchical-config (37).
- No upstream blockers.

---

*claude-sonnet-4-6*
