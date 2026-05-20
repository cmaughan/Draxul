---
# WI 72 — InputDispatcher Mouse Handler Triplication → Private Helper

**Type:** refactor  
**Priority:** medium (low risk; reduces maintenance surface)  
**Raised by:** [C] Claude  
**Created:** 2026-04-03  
**Model:** claude-sonnet-4-6

---

## Problem

`InputDispatcher` has three event handlers that each repeat the same 7-step dispatch pattern:

1. Forward event to `UiPanel` first
2. Check `UiPanel::wants_mouse()`
3. Check `contains_panel_point()` with scaled coordinates
4. Scale coordinates from window to host viewport
5. Check host exists and accepts mouse
6. Forward to host
7. Optionally propagate further

This pattern appears identically in:
- `on_mouse_button_event()`
- `on_mouse_move_event()`
- `on_mouse_wheel_event()`

Any change to the pattern (e.g. a new overlay check) requires three co-ordinated edits.

---

## Investigation Steps

- [x] Read `app/input_dispatcher.cpp` to confirm all three handlers share the same structure
- [x] Identify the exact commonality and the per-handler differences (button events have `button_state`, wheel events have `delta`, move events have coordinates only)
- [x] Design a private helper signature that parameterises the differences

---

## Proposed Helper

```cpp
// In input_dispatcher.cpp (private)
void InputDispatcher::dispatch_mouse_to_host(
    const glm::vec2& window_pos,
    std::function<void(IHost&, glm::vec2)> host_callback);
```

Each handler checks UiPanel interception, then calls `dispatch_mouse_to_host()` with a lambda that forwards the specific event fields.

---

## Implementation

- [x] Extract the common prefix/suffix into `InputDispatcher::dispatch_mouse_to_host()`
- [x] Simplify each of the three handlers to call the helper
- [x] Verify tests in `tests/input_dispatcher_test.cpp` (or equivalent) still pass unchanged

---

## Acceptance Criteria

- [x] Three handlers each reduced to ~5–8 lines
- [x] All existing input dispatcher tests pass
- [x] No behaviour change in any path (including `wants_mouse()` short-circuit)
- [x] Build and smoke test pass

---

## Notes

Standalone change — no interdependency with other WIs. Can be done by any agent in a single focused pass.
