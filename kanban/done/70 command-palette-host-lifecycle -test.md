---
# WI 70 — CommandPaletteHost Open/Close/Reopen Lifecycle Tests

**Type:** test  
**Priority:** medium (atlas state may leak across open/close cycles)  
**Raised by:** [P] GPT  
**Created:** 2026-04-03  
**Model:** claude-sonnet-4-6

---

## Problem

`CommandPaletteHost` is opened and closed frequently by the user. Its atlas dirty state and glyph handles may persist across cycles if not properly reset. Stale atlas handles or uncleared dirty state after close can cause:
- Glyphs from the previous session appearing in a freshly-opened palette
- Duplicate glyph rasterization on reopen
- Atlas dirty-rect uploads being skipped on the first post-open frame

---

## Investigation Steps

- [x] Read `app/command_palette_host.cpp` — handle is created on `dispatch_action("toggle")`
  open and destroyed via the palette's `on_closed` callback. `shutdown()` calls
  `palette_.close()` and `handle_.reset()`.
- [x] `flush_atlas_if_dirty()` reads from `TextService` and uploads via the renderer.
  Per-cycle atlas state is owned by `TextService`, not the host — so no host-side
  dirty-bit can leak across open/close.
- [x] `CommandPalette::open()` clears `query_` and resets `selected_index_`, so query
  state is reset on every reopen.
- [x] Used `FakeGridPipelineRenderer` (existing in `tests/support/`) — its
  `create_grid_handle_calls` counter is the leak guard.

---

## Test Cases

- [x] Open → close → `is_active()` flips false (handle released).
- [x] Open → close → reopen → `create_grid_handle_calls` increments to 2 (fresh handle).
- [x] Open → type → close → reopen → palette is responsive on the new cycle (query reset).
- [x] 100 rapid open/close cycles → exactly 100 handle allocations, no leak.
- [x] `shutdown()` while open releases the handle and clears `is_active()`.

---

## Implementation

- [x] Added 5 `[palette][lifecycle]` TEST_CASEs to `tests/command_palette_tests.cpp`
  (chosen over a new file so the existing palette harness/font init helper is reused).
- [x] No new fakes — `FakeGridPipelineRenderer::create_grid_handle_calls` already
  exists from WI 54.
- [x] Build: `cmake --build build --target draxul-tests`
- [x] Run: `./build/tests/draxul-tests "[lifecycle]"` — 11 cases / 442 assertions pass.

---

## Acceptance Criteria

- [x] After close + reopen, `is_active()` and the renderer reflect a fresh handle.
- [x] Handle count grows linearly (1 per open) across repeated cycles — verified at 100.
- [x] Rapid-open test completes without assertions or leaks.

---

## Notes

Closely related to the atlas upload path duplication noted in the review (CommandPaletteHost reimplements atlas dirty-rect upload). If WI 71 (atlas centralization) lands, update these tests to use the shared upload path.
