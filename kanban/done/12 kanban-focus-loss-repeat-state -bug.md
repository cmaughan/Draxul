# Stop Kanban navigation when the host loses focus
**Severity:** HIGH
**Source:** Codex #13; `modules/kanban/draxul-kanban/src/kanban_host.cpp:300`.

Holding navigation while switching tabs leaves repeat state active because the release reaches another host; background pumping continues selection changes.

**Investigation**

- [x] Trace held-key/navigation state, focus transitions, background pumping, and pinned preview callbacks.

**Fix strategy**

- [x] Clear repeat and navigation state on focus loss and guard unfocused repeat execution.

**Acceptance criteria**

- [x] Hold, switch tabs, and release produces no subsequent background selection or preview changes.
- [x] Refocusing requires fresh input and normal repeat still works; run core aggregate tests and same-cache smoke.

**Implementation and validation notes (2026-09-25):** Key release is routed to
the newly focused host after a tab switch, while the Kanban host continues to
pump. `on_focus_lost` now clears both the held repeat key and the navigation
prefix/delete latch; `pump_key_repeat` and repeat deadlines are gated on focus.
The repeat latch also refuses input delivered while unfocused, so refocusing
cannot revive it. The regression test enables pinned preview, holds navigation,
switches focus without a key-up, and checks that selection and preview remain
unchanged before and after refocus until a fresh key press. It also checks a
pending `g` prefix is cleared and normal held-key repeat resumes. A file-monitor
reconciliation may legitimately refresh the same pinned preview on refocus;
the regression test permits that while asserting no background preview change.

Final Windows validation: `py do.py test debug --products` passed 49/49 CTest
entries in 238.19 seconds, including the Kanban focus/repeat cases. The
standard fixed-30-second smoke timed out loading the existing nine-pane
Session; the same-cache smoke in the exact wrapper environment with a
90-second bound passed in approximately 45 seconds.
