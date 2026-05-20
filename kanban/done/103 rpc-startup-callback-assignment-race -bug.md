# WI 103 — rpc-startup-callback-assignment-race

**Type:** bug  
**Priority:** 1 (data race / UB during startup window)  
**Source:** review-bugs-consensus.md §H2 [Gemini]  
**Produced by:** claude-sonnet-4-6

---

## Problem

`NvimHost::initialize_host()` (`libs/draxul-host/src/nvim_host.cpp:34,40–45`) starts the reader
thread at line 34 (`rpc_.initialize()`), then assigns the callbacks on the main thread at lines
40–45:

```cpp
rpc_.initialize(nvim_process_);          // line 34 — reader thread starts here

rpc_.on_notification_available = [...];  // line 40 — assigned AFTER thread is running
rpc_.on_request = [...];                 // line 43
```

`std::function` is not thread-safe for concurrent reads and writes. Even though the reader thread
guards with `if (on_notification_available)` before calling, the concurrent read of the
`std::function` object by the reader thread while the main thread constructs it is a data race under
the C++ memory model — undefined behaviour.

Note: WI 89 covers a related but distinct issue (the `notif_mutex_` is still held when the callback
fires). This bug is about the *assignment ordering*, not the lock.

**Trigger:** Neovim sends a notification or request within milliseconds of startup — e.g. during the
`nvim_ui_attach` handshake on a fast machine. The window is narrow (~microseconds between
`initialize()` and the callback assignments) but is still UB.

---

## Investigation

- [ ] Read `libs/draxul-host/src/nvim_host.cpp:24–65` — confirm `rpc_.initialize()` is called
  before `on_notification_available` and `on_request` are assigned.
- [ ] Read `libs/draxul-nvim/src/rpc.cpp:282–284,248–251` — confirm the reader thread reads these
  members inside the dispatch methods.
- [ ] Check `libs/draxul-nvim/include/draxul/nvim_rpc.h` — confirm `on_notification_available` and
  `on_request` are plain `std::function` public members with no synchronisation.

---

## Fix Strategy

Move both callback assignments *before* `rpc_.initialize()`:

```cpp
rpc_.on_notification_available = [this]() { callbacks().wake_window(); };
rpc_.on_request = [this](const std::string& method, const std::vector<MpackValue>& params) {
    return handle_rpc_request(method, params);
};

if (!rpc_.initialize(nvim_process_))     // reader thread starts here — callbacks already set
{
    init_error_ = "Neovim exited unexpectedly during startup.";
    return false;
}
```

This eliminates the race entirely: the callbacks are fully constructed before any other thread can
read them.

- [ ] Apply the reordering.
- [ ] Verify `wire_ui_callbacks()` (line 47) still runs after `initialize()` — its ordering
  relative to the callbacks above should not change.
- [ ] Build: `cmake --build build --target draxul draxul-tests`
- [ ] Run smoke test: `py do.py smoke`

---

## Acceptance Criteria

- [ ] Both `on_notification_available` and `on_request` are assigned before `rpc_.initialize()` is
  called in `initialize_host()`.
- [ ] No `std::function` member of `NvimRpc` is written by the main thread while the reader thread
  is running.
- [ ] Smoke test passes.

---

## Interdependencies

- **WI 89** (rpc callback under `notif_mutex_`) — both touch `rpc.cpp` callback paths; fix in the
  same pass.
- **WI 101** (reader-thread exception guard) — also modifies `rpc.cpp`; combine.
- **WI 100** (partial-init terminate) — touches the same `initialize_host()` call site; fix
  together.
