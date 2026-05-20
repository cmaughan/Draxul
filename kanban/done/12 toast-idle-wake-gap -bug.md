# WI 12 — Background-thread toast sits invisible when app is idle

**Type:** bug  
**Severity:** HIGH  
**Source:** review-bugs-latest.gpt.md  
**Consensus:** review-consensus.md Phase 2

---

## Problem

The public contract for `ToastHost::push()` (`toast_host.h:21,28`) says background threads may call it and the toast will appear **"on the next frame"**. This contract is broken:

1. `ToastHost::push()` appends to `pending_` (`toast_host.cpp:10`) — correct.
2. `ToastHost::next_deadline()` **ignores pending work** (`toast_host.cpp:142`). It only considers active (already-showing) toasts.
3. `App::push_toast()` just forwards the message to `ToastHost` **without waking the event loop or requesting a new frame** (`app.cpp:1461`).

Result: if the app is idle (no input events, no nvim activity), a toast pushed from a background thread sits in `pending_` indefinitely and is never rendered until some unrelated event wakes the loop. In a fully idle session this can be **infinite delay**.

**Files:**
- `app/toast_host.cpp:10` (`push()`), `toast_host.cpp:142` (`next_deadline()`)
- `app/app.cpp:1461` (`App::push_toast()`)

---

## Implementation Plan

- [x] In `ToastHost::next_deadline()`: if `pending_` is non-empty, return `clock::now()` (or a very near deadline) so the scheduler wakes immediately.
- [x] In `App::push_toast()`: after forwarding to `ToastHost`, call the appropriate wake/request-frame mechanism (e.g. `request_frame_()` or post an SDL wakeup event) so the idle loop is kicked.
- [x] Ensure the wake call is thread-safe if `push_toast()` can be called from a background thread. (Uses `wake_window()` → `SDL_PushEvent`, documented thread-safe. `frame_requested_` is intentionally NOT touched from `push_toast()`; it is set on the main thread by `pump_once` once the wake causes the next_deadline re-check.)
- [x] Add a unit test (see WI 18) that:
  1. Constructs an app harness in idle state.
  2. Calls `push_toast()` from a background thread. (Exercised at the `ToastHost` layer since the ToastHost contract is what must hold; full cross-thread harness is deferred to WI 18.)
  3. Asserts a frame request or deadline update happens immediately, not after an input event.

## Resolution

Fixed in `app/toast_host.{h,cpp}` and `app/app.cpp`:

- `ToastHost::next_deadline()` now takes the `pending_mutex_` (now `mutable`) and returns `now()` whenever `pending_` is non-empty, so an idle scheduler re-checks immediately.
- `App::push_toast()` calls `wake_window()` after forwarding to `ToastHost`. `wake_window()` wraps `SDL_PushEvent` which is thread-safe, so the idle `SDL_WaitEvent` is kicked regardless of which thread queued the toast.
- New unit tests in `tests/toast_host_tests.cpp`: `next_deadline()` is empty when idle; a queued pending toast forces an immediate deadline; after pump the deadline returns to the ~33ms animation cadence.

---

## Interdependencies

- Fix before WI 18 (ToastHost idle wake delivery test).
- WI 11 (null grid handle) is a separate bug in the same file; fix both together.
- WI 121 (app-render-tree-overlay-ordering-test, active) exercises the toast rendering path; this fix must not break that test.

---

*Filed by: claude-sonnet-4-6 — 2026-04-08*
