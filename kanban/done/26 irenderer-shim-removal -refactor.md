# IRenderer Combined Shim Removal

**Type:** refactor
**Priority:** 26
**Raised by:** Claude

## Summary

`App` currently holds a `std::unique_ptr<IRenderer>` (the combined shim) and then casts it to three raw focused-interface pointers from the same object. Now that the focused interfaces exist, the combined shim adds complexity without value. Replace the combined class with direct storage of the three focused interface pointers obtained from the renderer factory.

## Background

The historical reason for a combined `IRenderer` interface was likely that the renderer was created as a single object before focused interfaces were introduced. The focused interfaces now exist, and the factory already creates a concrete renderer. The App's continued use of the combined shim plus three casts is purely an artefact of the migration — it provides no abstraction benefit and makes the renderer ownership model confusing (who owns what, which pointer to use when).

## Implementation Plan

### Files to modify
- `app/app.h` — replace `std::unique_ptr<IRenderer> renderer_` and the three raw pointer fields with three `std::unique_ptr` or owning wrappers for the focused interfaces
- `app/app.cpp` — update `initialize()` and all renderer usage to go through the focused interfaces directly
- `app/renderer_factory.cpp` — update factory to return focused interface instances (or a struct wrapping the three)
- `libs/draxul-renderer/include/draxul/renderer.h` — remove or deprecate the combined `IRenderer` if no other consumer uses it

### Steps
- [x] Survey all usages of the combined `IRenderer` pointer in `app.cpp`; list which focused interface each call belongs to
- [x] Design the replacement ownership model: introduced `RendererBundle` struct in `renderer.h` wrapping `unique_ptr<IRenderer>` with typed accessors `grid()`, `imgui()`, `capture()`
- [x] Update `renderer_factory.cpp` to return `RendererBundle`
- [x] Update `App` to store `RendererBundle renderer_`; update all call sites
- [x] `IRenderer` retained in `renderer.h` (tests use `FakeRenderer : public IRenderer`); `RendererBundle` is now the factory return type and App's storage type
- [x] Also simplified `HostManager::Deps::grid_renderer` from `IGridRenderer**` to `IGridRenderer*`, and `GuiActionHandler::Deps::imgui_host` from `IImGuiHost**` to `IImGuiHost*`
- [x] Verify build succeeds for macOS (build clean, no errors)
- [x] Run `ctest` and smoke tests (all 97 tests pass, smoke test exits 0)

## Depends On
- `31 app-class-decomposition -refactor.md` — the App decomposition may further reorganise how renderer interfaces are held; consider doing this after or as part of that work

## Blocks
- None

## Notes
If `IRenderer` is still used by tests or other consumers outside `App`, do not remove it — just stop using it in `App`. The combined interface can coexist with focused interfaces during a transition period. Tests continue to use `IRenderer` via `FakeRenderer`, so it is retained.

> Work item produced by: claude-sonnet-4-6
