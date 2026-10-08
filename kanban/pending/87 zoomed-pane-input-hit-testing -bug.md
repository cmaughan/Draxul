# Route zoomed input to the visible pane
**Summary:** Keep mouse input on the visible zoomed pane so typing cannot move to a hidden pane.

**Priority:** P1  
**Source:** `app/pane_manager.cpp`  
**Reported by:** Claude H2; consensus F23.

**Evidence and trigger:** Lines 1193–1197 hit-test normal split rectangles and update focus while zoomed. Mouse movement can select a hidden leaf; hidden divider hits also capture input.

**Related:** `kanban/pending/78 zoomed-pane-print-crop -bug.md`.

- [x] **Investigate:** Trace zoomed leaf identity, mouse movement/buttons/wheels, focus, and divider hit-testing.
  - Confirmed on 2026-10-08: `InputDispatcher` routes every mouse move, button, and
    wheel through `PaneManager::host_at_point()`, which hit-tested the hidden split
    rectangles and called `update_focus()`. `divider_at_point()` also hit-tested the
    hidden split, so hover changed the cursor and a press captured a drag (the ratio
    update itself was already guarded). Separately, `focus_direction()` and other
    `set_focused()` callers could focus a hidden leaf while zoomed, sending typing to it.
- [x] **Fix:** Resolve zoomed input against displayed geometry and suppress hidden dividers.
  - `host_at_point()` returns the zoomed host (which covers the whole content area)
    without consulting split geometry; `divider_at_point()` returns nothing while
    zoomed; `update_focus()` carries the zoom to an explicitly focused leaf so the
    focused pane is always the visible one; restore aligns stale checkpoint focus with
    the restored zoomed leaf.
- [x] **Acceptance:** Moving or clicking across a zoomed pane never focuses hidden panes or captures hidden dividers.
- [x] **Acceptance:** Unzooming restores normal split hit-testing and intended focus.
  - Covered by `zoomed pane owns pointer and keyboard input over hidden split geometry`
    in `tests/input_dispatcher_routing_tests.cpp`, which drives real window events
    through `InputDispatcher` into a real `PaneManager`.
- [ ] **Validation:** Run core aggregate tests and same-cache smoke; check zoomed input interactively.
  - Aggregate tests and smoke: see the commit's validation summary. The interactive
    check of zoomed input in a live window is still outstanding (headless agent run).

**Shared with `kanban/pending/78 zoomed-pane-print-crop -bug.md`:** both bugs come
from callers reading the stored split rectangle instead of the displayed zoomed
viewport. The displayed geometry is `{0, 0, zoom_pixel_w_, zoom_pixel_h_}` passed
through `compute_viewport` for `zoomed_leaf()`; 78 should snapshot that same viewport
(or the zoomed host's current viewport) rather than `tree().descriptor_for(leaf)`.
