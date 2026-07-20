# Clarify GUI/UI contracts and UiPanel lifecycle

**Type:** refactor
**Priority:** 29
**Raised by:** Claude

## Goal

Make the valid `draxul-gui` versus `draxul-ui` split understandable and remove overlapping names/lifecycle APIs: cell-grid native overlays versus ImGui diagnostics.

## Implementation plan

- [x] Add short READMEs/target comments defining ownership, dependencies, and when code belongs in each library.
- [x] Rename the private palette `PanelLayout` to a domain-specific name; avoid broad public renames initially. (`PaletteLayout` / `compute_palette_layout` in `libs/draxul-gui/src/palette_renderer.cpp`.)
- [x] Choose one canonical `UiPanel` frame-driving API (`render` or explicit begin/end) and migrate all callers. (`render()` is canonical; `begin_frame`/`end_frame`/`render_into_current_context` removed.)
- [x] Replace undocumented raw `IImGuiHost*` lifetime with an explicit attach/detach token or reference contract. (`attach_imgui_backend(IImGuiHost&)` / `detach_imgui_backend()`, contract documented on the header declarations.)
- [x] Separate layout/state from ImGui rendering where pure tests add value. (Pure `compute_panel_layout` / `PanelLayout` remain ImGui-free and covered by the layout/DPI tests; lifecycle now separately covered too.)
- [x] Remove dead/overlapping entry points only after all callers and tests migrate. (`set_imgui_backend`, `activate_imgui_context`, `begin_frame`, `end_frame`, `render_into_current_context` all removed; sole caller `app/diagnostics_panel_host.cpp` migrated.)
- [x] Update architecture docs when the contract is final. (Per-library `README.md` for `draxul-gui` and `draxul-ui` document the split, dependency direction, and the UiPanel contracts.)

## Tests and acceptance

- [x] Existing diagnostics/panel layout/input-capture tests cover the canonical API.
- [x] Attach/detach/shutdown order is tested with a fake backend. (`tests/ui_panel_backend_tests.cpp`, tag `[imgui_backend]`, auto-discovered by the tests glob.)
- [x] A contributor can determine the correct library from the docs and dependency direction.
- [x] No visual behavior change; full tests/render snapshots/smoke pass. (macOS/Metal: build, full `ctest` 11/11 incl. `draxul-render-panel-view`, and `do.py smoke` all green. Windows/Vulkan build + render refs pending CI.)

## Status — 2026-07-19 (macOS)

Finished the reconciliation the prior agent left mid-edit (header/cpp/caller had briefly drifted; now consistent). The settled contract:

- **Canonical frame API:** `UiPanel::render(IFrameContext&, float)` runs the whole ImGui frame internally (set context current → `begin_imgui_frame()` → `NewFrame()` → draw → `Render()` → `IFrameContext::render_imgui()`). No public begin/end pair remains.
- **Backend lifetime:** panel never owns the backend. `attach_imgui_backend(IImGuiHost&)` makes the panel context current, calls `initialize_imgui_backend()`, and stores a reference; `detach_imgui_backend()` (also implied by `shutdown()`/destruction) calls `shutdown_imgui_backend()` exactly once per attachment. Attaching a new backend detaches the previous first; re-attaching the same backend is a no-op.
- **Double-init is safe:** `app/app.cpp` still calls `initialize_imgui_backend()` directly after `attach_imgui_host()`. Both Metal and Vulkan backends guard on `ImGui::GetIO().BackendRendererUserData`, so the follow-up call is a guarded no-op that still surfaces init failures. No app-flow change was needed.

Validation (macOS/Metal, Debug): `cmake --build build --target draxul draxul-tests` clean; `ctest --test-dir build` 11/11 (5 render snapshots incl. `draxul-render-panel-view`, 4 unit shards, smoke, do-py); `python3 do.py smoke` exit 0. Windows/Vulkan is left to CI.

Note: the card lists "Item 08 should land first" as a dependency, but no item 08 exists in `kanban/pending` or `kanban/done`. This work is self-contained and does not depend on it in the current tree.

## Dependencies and parallelism

Item 08 should land first. This can run separately from Chrome once shared overlay helpers are stable.

<model>GPT-5 Codex</model>
