# Keep pointer drags with the pane where they started

**Summary:** Send every move and the release of a mouse drag to the pane where the button was pressed, so dragging an ImGui window (or a selection) past its pane no longer leaves it stuck mid-drag, and let render tests script real input to reproduce such problems.

**Priority:** P1 — MegaCity ImGui window moves/docking misbehave and were reported as crashes (user report, 2026-10-10).

**Related:** `plugins/megacity/kanban/pending/17 debug-window-pane-clipping -bug.md` (pane-local ImGui coordinates are the follow-up slice); `kanban/pending/122 render-test-plugin-storage-isolation -bug.md`.

- [x] Investigate: the input dispatcher routed each move/release to whichever pane was under the pointer and suppressed events over chrome, so a product ImGui context never saw the release of a drag that left its pane (the window kept following the cursor) and focus moved with the pointer mid-drag.
- [x] Fix: capture the pointer on a host's button press until all its buttons are released; release capture when the host is cleared before destruction or an overlay opens; keep the diagnostics panel's ImGui button state in sync.
- [x] Add render-test `input` steps (move/down/up/drag/key/wait) replayed as SDL events after settle, and request a post-hook frame so a hook that already rendered cannot idle until the timeout.
- [x] Reproduce MegaCity window drags, tab docking, split docking and edge drags through the harness against the 1,128-file Draxul city with ImGui assertions enabled: no assertion or crash on macOS/Metal.
- [x] Cover capture across a split, out-of-window moves, host release and overlay activation in input-dispatcher routing tests; run the core aggregate and same-cache smoke.
- [ ] Confirm with the user's interactive MegaCity session that the reported crash no longer occurs, or capture a `--log-file` trace if it does (the process crash was not reproduced and left no macOS crash report).
