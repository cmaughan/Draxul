# WI 101 — reader-thread-uncaught-exception

**Type:** bug  
**Priority:** 0 (process abort on malformed msgpack packet)  
**Source:** review-bugs-consensus.md §C2 [Gemini, GPT]  
**Produced by:** claude-sonnet-4-6

---

## Problem

`NvimRpc::reader_thread_func()` (`libs/draxul-nvim/src/rpc.cpp:304–363`) has no `try/catch` block.
`dispatch_rpc_message()` is called at line 361 and immediately calls:

```cpp
auto type = (int)msg_array[0].as_int();   // line 294
```

`MpackValue::as_int()` (`libs/draxul-nvim/include/draxul/nvim_rpc.h:93–100`) throws
`std::bad_variant_access` if the value is not `int64_t` or `uint64_t`. The same applies to every
other `as_*()` call inside the dispatch chain.

An uncaught exception on a `std::thread` calls `std::terminate()` — the entire process aborts.

**Trigger:** A valid-msgpack but wrongly-typed top-level packet from Neovim (e.g. protocol
extension, version drift, or a corrupted pipe segment).

---

## Investigation

- [ ] Read `libs/draxul-nvim/src/rpc.cpp:304–363` — confirm there is no `try/catch` around
  `dispatch_rpc_message`.
- [ ] Read `libs/draxul-nvim/include/draxul/nvim_rpc.h:93–126` — confirm `as_int()`, `as_str()`,
  `as_array()`, `as_map()` all throw on type mismatch.
- [ ] Trace all `as_*()` calls reachable from `dispatch_rpc_message` on the reader thread.
- [ ] Confirm that setting `read_failed_ = true` and calling `response_cv_.notify_all()` is
  sufficient to trigger the orderly shutdown path used by EOF handling.

---

## Fix Strategy

- [ ] Wrap `dispatch_rpc_message(msg)` at line 361 with:

```cpp
try
{
    dispatch_rpc_message(msg);
}
catch (const std::exception& e)
{
    DRAXUL_LOG_ERROR(LogCategory::Rpc,
        "Malformed RPC packet; aborting connection: %s", e.what());
    impl_->read_failed_ = true;
    impl_->running_ = false;
    impl_->response_cv_.notify_all();
    if (on_notification_available)
        on_notification_available();
    break;
}
```

- [ ] Verify that `on_notification_available` is safe to call after setting `running_ = false`
  (consistent with the EOF path at lines 326–330).
- [ ] Build: `cmake --build build --target draxul draxul-tests`
- [ ] Run: `ctest --test-dir build -R rpc` (if applicable)
- [ ] Run smoke test: `py do.py smoke`

---

## Acceptance Criteria

- [ ] A malformed msgpack packet on the RPC pipe results in an error log and a clean disconnect,
  not `std::terminate`.
- [ ] The reader thread exits cleanly and the main thread detects the disconnect via
  `connection_failed()`.
- [ ] Smoke test passes.

---

## Interdependencies

- **WI 100** (partial-init terminate) — the tear-down path triggered by this fix must be safe;
  fix WI 100 first.
- **WI 89** (rpc callback under lock) — both modify `rpc.cpp`; combine into one pass.
- **WI 103** (startup callback race) — also modifies `rpc.cpp`; combine.
- **WI 102** (ui_events type guards) — complementary defensive-typing fix for the main thread.
