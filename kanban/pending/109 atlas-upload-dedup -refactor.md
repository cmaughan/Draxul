# WI 109 — atlas-upload-dedup

**Type:** refactor
**Priority:** 6 (code quality — fragile copy-paste upload logic across three subsystems)
**Source:** review-consensus.md §3 [GPT]
**Produced by:** claude-sonnet-4-6

---

## Problem

The glyph-atlas dirty-upload logic is copy-pasted across three independent code paths:

| Location | Role |
|----------|------|
| `libs/draxul-runtime-support/src/grid_rendering_pipeline.cpp:228` | Grid host atlas upload |
| `app/chrome_host.cpp:424` | Chrome tab bar atlas upload |
| `app/command_palette_host.cpp:182` | Command palette atlas upload |

Each path independently inspects atlas dirty state and clears it. There is no guarantee of ordering: if two paths fire in the wrong order, one may clear dirty state before the other has uploaded, causing stale glyph rendering. Adding a fourth glyph consumer (e.g., a status bar or diagnostics panel) requires a fourth copy.

---

## Investigation

- [x] Read each of the three files at the listed lines — understand the exact upload steps, what state they read and write, and whether any of the three has additional logic beyond a plain "upload dirty regions and clear dirty flag."
- [x] Determine whether the atlas dirty state is owned by the renderer or a shared object; confirm who is allowed to clear it and when.
- [x] Check `IGridRenderer` or `RendererBundle` for any existing "upload atlas if dirty" hook.

---

## Fix Strategy

- [x] Extract a single `upload_atlas_if_dirty(IGridRenderer& renderer)` helper (or equivalent method on the renderer/atlas interface) that:
  1. Checks whether any regions are dirty.
  2. Uploads all dirty regions.
  3. Clears dirty state.
- [x] Call this helper exactly once per frame from a single call site (e.g., the frame orchestration loop in `App::render_frame()` or `grid_rendering_pipeline`), before any subsystem renders.
- [x] Remove the duplicate atlas-upload code from `chrome_host.cpp` and `command_palette_host.cpp`.
- [ ] Write WI 108 (atlas-dirty-multi-subsystem -test) to validate the refactor.
- [x] Build: `cmake --build build --target draxul draxul-tests`
- [x] Run smoke: `py do.py smoke`

---

## Acceptance Criteria

- [x] Atlas upload logic exists in exactly one place.
- [x] `chrome_host.cpp` and `command_palette_host.cpp` no longer inspect or clear atlas dirty state directly.
- [x] No visual regressions in render snapshot tests.
- [ ] WI 108 test passes.

---

## Interdependencies

- **WI 108** (atlas-dirty-multi-subsystem -test) — write the test alongside this refactor.
