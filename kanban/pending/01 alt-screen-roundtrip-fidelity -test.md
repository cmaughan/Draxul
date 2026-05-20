# WI 135: Alt-Screen Round-Trip Fidelity Test

**Type:** test
**Priority:** 01
**Raised by:** Claude (`review-latest.claude.md`) — test coverage gap
**Depends on:** None (independent; uses `replay_fixture.h` — no Neovim required)

---

## Problem

The existing `altscreen-resize-mismatch` bug (WI 06, complete) was a bug fix, not a correctness test. There is currently no test that:

1. Populates the main screen with known content.
2. Switches to the alt screen (`ESC[?1049h`).
3. Modifies the alt screen.
4. Switches back to the main screen (`ESC[?1049l`).
5. Verifies the main screen content is cell-for-cell identical to what it was before the alt-screen switch.

Without this test, regressions in alt-screen save/restore — e.g. a resize during alt-screen clobbering saved main-screen cells — will not be caught until a user reports garbled output.

---

## What to Test

- [ ] Main screen populated with known `grid_line` content (cells with specific text, highlight IDs, double-width markers).
- [ ] `ESC[?1049h` switches to alt screen; verify alt screen starts blank (or with whatever the VT spec requires).
- [ ] Write different content to the alt screen via `grid_line` events.
- [ ] `ESC[?1049l` returns to main screen.
- [ ] Assert every cell in main screen matches the pre-switch snapshot exactly.
- [ ] Repeat with an intermediate resize event between `ESC[?1049h` and `ESC[?1049l` to catch the resize-clobbering regression class.
- [ ] Repeat with cursor position save/restore (`DECSC`/`DECRC`) interleaved to verify cursor state is independent of alt-screen state.

---

## Implementation Plan

1. **Use `ReplayFixture`** from `tests/support/replay_fixture.h`. It provides msgpack-like builders for `grid_line` and `redraw` events — no real Neovim needed.

2. **Instantiate a `UiEventHandler` with a `Grid`.**
   ```cpp
   Grid grid(80, 24);
   UiEventHandler handler(grid, ...);
   ```

3. **Build the main screen state** using `replay_fixture` helpers:
   ```cpp
   auto events = make_redraw_events({
       make_grid_line(1, 0, 0, {{"Hello", 1}, {" world", 2}}),
       // ... fill several rows
   });
   handler.process_redraw(events);
   ```
   Snapshot the grid state.

4. **Inject alt-screen switch** via `UiEventHandler::process_redraw()` with a `mode_change` event or the appropriate OSC/CSI sequence through the VT parser path:
   ```
   ESC [ ? 1049 h   → alt screen on
   ```

5. **Write alt-screen content**, then switch back:
   ```
   ESC [ ? 1049 l   → alt screen off
   ```

6. **Compare grid cells** against the pre-switch snapshot. Use `EXPECT_EQ(grid.cell(r, c), snapshot.cell(r, c))` for each cell.

7. **Add the resize variant:** send a `grid_resize` event between the two switches, then verify restoration.

---

## Files Likely Involved

- `tests/ui_event_handler_tests.cpp` or new `tests/alt_screen_tests.cpp`
- `tests/support/replay_fixture.h`
- `libs/draxul-host/src/vt_parser.cpp` (the ESC[?1049 handler)
- `libs/draxul-grid/include/draxul/grid.h` (cell snapshot comparison)

---

## Acceptance Criteria

- [ ] Test passes under `ctest -R alt_screen` (or the chosen binary name).
- [ ] The resize-interleaved variant also passes.
- [ ] Tests run in < 100ms without spawning any process.
- [ ] Tests pass under `mac-asan` with no memory errors.

---

*Authored by `claude-sonnet-4-6`*
