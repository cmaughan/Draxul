# 15 appconfig-sdl-decoupling -refactor

**Priority:** MEDIUM
**Type:** Refactor
**Raised by:** Claude, Gemini
**Model:** claude-sonnet-4-6

---

## Problem

`libs/draxul-app-support/include/draxul/app_config.h:20–26` — `GuiKeybinding::key` is typed as `SDL_Keycode` (or a comment indicates it's an SDL type). This pulls SDL3 as a transitive dependency of `draxul-app-support`, a library that is otherwise platform-neutral. Tests and other consumers of `AppConfig` must link against SDL even if they don't use a window.

The `app-support-module-boundary` refactor is marked complete, but this specific SDL type leak in the public header appears to remain.

---

## Implementation Plan

- [ ] Read `libs/draxul-app-support/include/draxul/app_config.h` in full.
- [ ] Read `libs/draxul-app-support/CMakeLists.txt` to check whether SDL is a `PUBLIC` or `PRIVATE` dependency.
- [ ] Identify all SDL types used in public headers of `draxul-app-support`.
- [ ] Replace `SDL_Keycode` (or any SDL type) in the `AppConfig` public header with a platform-neutral equivalent:
  - Option A: Use a plain `int32_t` key code with a comment mapping to SDL values at the conversion boundary.
  - Option B: Define a `DraxulKeycode` typedef or enum and convert from/to SDL at the `SdlWindow` layer only.
- [ ] Update `CMakeLists.txt` so SDL is a `PRIVATE` dependency of `draxul-app-support` (or removed entirely if the type is removed).
- [ ] Fix all compilation breaks in `app/` and `tests/` that result from the type change.
- [ ] Run clang-format on all touched files.
- [ ] Build and run smoke test + ctest.

---

## Acceptance

- `draxul-app-support` public headers include no SDL headers.
- SDL is not a transitive link dependency for consumers of `draxul-app-support`.
- All tests still compile and pass.

---

## Interdependencies

- Potentially unblocks cleaner window abstraction if `SdlWindow` can hold all SDL knowledge.
- No upstream dependencies on other new work items.
- Feeds into **19-feature** (per-monitor DPI scaling) if that feature needs clean config-layer plumbing.
