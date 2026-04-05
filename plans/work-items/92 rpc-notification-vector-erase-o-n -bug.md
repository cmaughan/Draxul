# WI 92 — rpc-notification-vector-erase-o-n

**Type:** bug  
**Priority:** 1 (main-thread starvation under high nvim output)  
**Source:** review-bugs-consensus.md §H6 [Claude]  
**Produced by:** claude-sonnet-4-6

---

## Problem

In `libs/draxul-nvim/src/rpc.cpp:268–269`, when the 4 096-element notification queue is full, the oldest entry is dropped via `notifications_.erase(notifications_.begin())`. This is an O(4 096) memory shift while holding `notif_mutex_`. The main thread takes the same mutex in `drain_notifications()`. Under sustained nvim output (large file ops, mass diagnostics), each eviction stalls the main thread for milliseconds, causing frame drops.

---

## Investigation

- [ ] Read `libs/draxul-nvim/src/rpc.cpp:260–285` — confirm `notifications_` is a `std::vector` and `erase(begin())` is the eviction path.
- [ ] Confirm `drain_notifications()` acquires the same `notif_mutex_` and that the two mutexes are the same object.
- [ ] Measure (or estimate) the cost of a 4 096-element vector shift vs a `deque` front pop.

---

## Fix Strategy

- [ ] Change `notifications_` from `std::vector<RpcNotification>` to `std::deque<RpcNotification>` in the `Impl` struct.
- [ ] Replace `notifications_.erase(notifications_.begin())` with `notifications_.pop_front()`.
- [ ] Verify `drain_notifications()` uses only back-insertion and front-removal (no random access) so `deque` semantics are compatible.
- [ ] Build: `cmake --build build --target draxul draxul-tests`
- [ ] Run: `ctest --test-dir build -R rpc`
- [ ] Run smoke test: `py do.py smoke`

---

## Acceptance Criteria

- [ ] Notification queue eviction is O(1).
- [ ] RPC unit tests pass.
- [ ] Smoke test passes.

---

## Interdependencies

- **WI 89** (rpc-notification-callback-under-lock) — both modify the notification queue path in `rpc.cpp`; combine.
- **WI 83** (clipboard-sdl-reader-thread) — same file; combine into one pass.
