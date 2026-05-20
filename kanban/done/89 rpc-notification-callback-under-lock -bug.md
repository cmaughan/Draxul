# WI 89 — rpc-notification-callback-under-lock

**Type:** bug  
**Priority:** 1 (potential deadlock)  
**Source:** review-bugs-consensus.md §H3 [Claude]  
**Produced by:** claude-sonnet-4-6

---

## Problem

In `libs/draxul-nvim/src/rpc.cpp:263–284`, the `std::lock_guard<std::mutex>` taken at line 265 is still in scope when `on_notification_available()` is called at lines 283–284. The main thread takes the same `notif_mutex_` in `drain_notifications()`. If the callback path ever acquires any mutex that the main thread holds during `drain_notifications()`, the result is a deadlock cycle.

Currently `on_notification_available` calls `callbacks().wake_window()` → `window_->wake()` (push SDL event), which does not deadlock in practice. However, the pattern is fragile and inconsistent with the EOF/error path at lines 328–329 which explicitly releases the lock before calling the callback.

---

## Investigation

- [ ] Read `libs/draxul-nvim/src/rpc.cpp:260–335` — confirm the lock is held at the callback site and that the error path releases it first.
- [ ] Trace `on_notification_available` → `wake_window()` → `SDL_PushEvent()` — confirm no mutex is taken in this path that the main thread could hold during `drain_notifications()`.
- [ ] Check whether any other `on_notification_available` implementations exist in the codebase that could introduce a mutex.

---

## Fix Strategy

- [ ] Restructure `dispatch_rpc_notification()` so the lock is released before calling `on_notification_available()`, matching the error path at line 328:
  ```cpp
  {
      std::lock_guard<std::mutex> lock(impl_->notif_mutex_);
      // ... push notification, warn if full ...
  }
  // lock released
  if (on_notification_available)
      on_notification_available();
  ```
- [ ] Build: `cmake --build build --target draxul draxul-tests`
- [ ] Run: `ctest --test-dir build -R rpc` to confirm RPC tests pass.

---

## Acceptance Criteria

- [ ] `on_notification_available()` is never called while `notif_mutex_` is held.
- [ ] RPC unit tests pass.
- [ ] Smoke test passes.

---

## Interdependencies

- **WI 83** (clipboard-sdl-reader-thread) — both touch `rpc.cpp`; combine into one pass.
- **WI 92** (rpc-notification-vector-erase-o-n) — also modifies the notification queue path in `rpc.cpp`; combine.
