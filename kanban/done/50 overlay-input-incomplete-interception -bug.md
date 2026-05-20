# WI 50 — overlay-input-incomplete-interception

**Type**: bug  
**Priority**: 2 (command palette leaks events to underlying host)  
**Source**: review-consensus.md §B3 [P][G]  
**Produced by**: claude-sonnet-4-6

---

## Problem

`InputDispatcher::on_key_event()` (`app/input_dispatcher.cpp:71`) contains a comment saying the overlay host intercepts all input. This is false. The following paths ignore `overlay_host` entirely:

- Mouse button events — `app/input_dispatcher.cpp:144`
- Mouse move/motion events — `app/input_dispatcher.cpp:168`
- Mouse wheel events — `app/input_dispatcher.cpp:191`
- `TextEditingEvent` (IME composition) — `app/input_dispatcher.cpp:267`

When the command palette is open, scrolling the underlying terminal pane with the wheel and typing IME characters still reaches the active host. This is surprising behaviour and makes the palette feel like a non-modal overlay.

---

## Tasks

- [x] Read `app/input_dispatcher.cpp` fully — map all event-handling paths and mark which ones check `overlay_host_` before dispatching.
- [x] Read `app/input_dispatcher.h` — understand the `overlay_host_` field and how it is set/cleared.
- [x] For each of the four leaking paths (mouse button, mouse move, mouse wheel, `TextEditingEvent`): add an early-return if `overlay_host_` is set, consistent with how key events are handled.
- [x] Verify that `on_mouse_move` still works for overlay-internal drag actions if the overlay itself processes mouse move (e.g., scrolling the palette list). The overlay host's own `on_mouse_move` should receive the event; the underlying host should not.
- [x] Update the comment in `on_key_event()` to accurately describe the interception contract.
- [x] Build and run smoke test: `cmake --build build --target draxul draxul-tests && py do.py smoke`

---

## Acceptance Criteria

- When `overlay_host_` is non-null, mouse button, wheel, and `TextEditingEvent` paths do not reach the underlying pane host.
- The overlay host itself still receives events it needs (e.g., mouse move for palette scroll, key for search typing).
- Smoke test passes.
- No new test failures.

---

## Interdependencies

- **WI 53** (overlay-input-routing-tests) — regression test for this fix; file both in the same agent pass.

---

## Notes for Agent

- Read the code before writing; the interception logic for key events is the model to follow — replicate its pattern.
- Be careful with mouse-move: some overlays may do internal dragging; the overlay host should still receive moves when it is active.
- This is a focused change confined to `app/input_dispatcher.cpp` (and possibly `.h`).
