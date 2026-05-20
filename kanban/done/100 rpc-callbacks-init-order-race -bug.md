# WI 100 — rpc-callbacks-init-order-race

**Type:** bug
**Priority:** 0 (CRITICAL — startup crash / undefined behaviour)
**Source:** review-consensus.md §2 [Gemini — overlooked in WI 82–99 triage]
**Produced by:** claude-sonnet-4-6

---

## Problem

`NvimRpc::initialize()` starts the reader thread. `NvimHost::initialize_host()` calls `rpc_.initialize()` and **then** assigns `rpc_.on_notification_available` and `rpc_.on_request`. There is no synchronization between the two operations.

If Neovim emits a notification or request during the startup window (e.g., immediately after `nvim_ui_attach`), the reader thread reads a default-constructed (null) `std::function` and invokes it, causing a crash. Even if the function happens to be assigned before the first message, `std::function` assignment is not atomic — the read and write race.

**Files:**
- `libs/draxul-nvim/src/rpc.cpp` — `reader_thread_func` reads `on_notification_available` / `on_request`
- `libs/draxul-nvim/include/draxul/nvim_rpc.h` — public `std::function` members
- `libs/draxul-host/src/nvim_host.cpp` — `initialize_host()` sets callbacks after `rpc_.initialize()`

---

## Investigation

- [ ] Read `libs/draxul-nvim/include/draxul/nvim_rpc.h` — confirm `on_notification_available` and `on_request` are public non-atomic `std::function` members.
- [ ] Read `libs/draxul-nvim/src/rpc.cpp` (reader_thread_func) — confirm callbacks are read without any lock.
- [ ] Read `libs/draxul-host/src/nvim_host.cpp` (initialize_host) — confirm the ordering: `rpc_.initialize()` is called before `rpc_.on_notification_available = ...`.

---

## Fix Strategy

Option A — set callbacks **before** starting the reader thread:
- [ ] In `NvimHost::initialize_host()`, assign `rpc_.on_notification_available` and `rpc_.on_request` before calling `rpc_.initialize()`.
- [ ] Verify `rpc_.initialize()` does not call the callbacks itself before the thread starts.

Option B — protect with a mutex (if Option A is architecturally difficult):
- [ ] Add a `std::mutex callbacks_mutex_` to `NvimRpc::Impl`; take it before reading or writing the callback members.

Option A is preferred — zero overhead, fixes the race by construction.

- [ ] Build: `cmake --build build --target draxul draxul-tests`
- [ ] Run smoke: `py do.py smoke`

---

## Acceptance Criteria

- [ ] `on_notification_available` and `on_request` are fully initialized before the reader thread can invoke them.
- [ ] No `std::function` member is read without prior synchronization if it might still be null.
- [ ] Smoke test passes.

---

## Interdependencies

- **WI 89** (rpc-notification-callback-under-lock) — both touch the notification dispatch path in `rpc.cpp`; batch in the same pass.
- **WI 101** (mpackvalue-reader-thread-uncaught) — same file; fix together.
