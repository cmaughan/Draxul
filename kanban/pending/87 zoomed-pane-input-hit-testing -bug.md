# Route zoomed input to the visible pane
**Summary:** Keep mouse input on the visible zoomed pane so typing cannot move to a hidden pane.

**Priority:** P1  
**Source:** `app/pane_manager.cpp`  
**Reported by:** Claude H2; consensus F23.

**Evidence and trigger:** Lines 1193–1197 hit-test normal split rectangles and update focus while zoomed. Mouse movement can select a hidden leaf; hidden divider hits also capture input.

**Related:** `kanban/pending/78 zoomed-pane-print-crop -bug.md`.

- [ ] **Investigate:** Trace zoomed leaf identity, mouse movement/buttons/wheels, focus, and divider hit-testing.
- [ ] **Fix:** Resolve zoomed input against displayed geometry and suppress hidden dividers.
- [ ] **Acceptance:** Moving or clicking across a zoomed pane never focuses hidden panes or captures hidden dividers.
- [ ] **Acceptance:** Unzooming restores normal split hit-testing and intended focus.
- [ ] **Validation:** Run core aggregate tests and same-cache smoke; check zoomed input interactively.
