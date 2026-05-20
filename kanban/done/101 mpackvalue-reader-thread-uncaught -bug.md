# WI 101 — mpackvalue-reader-thread-uncaught

**Type:** bug
**Priority:** 0 (CRITICAL — reader thread death silently freezes the app)
**Source:** review-consensus.md §2 [Gemini — overlooked in WI 82–99 triage]
**Produced by:** claude-sonnet-4-6

---

## Problem

`MpackValue::as_int()`, `as_string()`, `as_array()`, and sibling accessors throw `std::bad_variant_access` if the underlying `std::variant` holds a different type. These methods are called inside `dispatch_rpc_response()`, `dispatch_rpc_notification()`, and `dispatch_rpc_request()`, all of which run on the RPC reader thread.

There is no `try/catch` in `reader_thread_func`. A single malformed or unexpected msgpack packet from Neovim (or a Neovim plugin) will propagate the exception up through the call chain, terminate `reader_thread_func`, and kill the reader thread permanently. The app then hangs waiting for notifications that will never arrive — with no visible error message to the user.

**Files:**
- `libs/draxul-nvim/src/rpc.cpp` — `reader_thread_func` (entry point for the reader thread)
- `libs/draxul-nvim/include/draxul/nvim_rpc.h` — `MpackValue::as_*()` throw on type mismatch
- `libs/draxul-nvim/src/ui_events.cpp` — calls `as_*()` in Neovim UI event processing

---

## Investigation

- [ ] Read `libs/draxul-nvim/src/rpc.cpp` — find `reader_thread_func` and confirm there is no top-level `try/catch`.
- [ ] Search for `as_int()`, `as_string()`, `as_array()` calls on the reader-thread code path — confirm none are already guarded.
- [ ] Check whether `MpackValue` provides a non-throwing alternative (e.g., `get_if`) that could be used instead.

---

## Fix Strategy

### Immediate defensive fix
- [ ] Wrap the body of `reader_thread_func` in `try { ... } catch (const std::exception& e) { DRAXUL_LOG_ERROR(..., "RPC reader thread exception: {}", e.what()); }` — log and break the loop so `read_failed_` is set and the main thread sees the disconnection gracefully.

### Deeper fix (preferred)
- [ ] Where `as_*()` is called on untrusted msgpack data on the reader thread, replace throws with `std::get_if<T>()` and early-return / log an error. This eliminates the exception path entirely rather than just catching it.

- [ ] Build: `cmake --build build --target draxul draxul-tests`
- [ ] Run: `ctest --test-dir build -R rpc`
- [ ] Run smoke: `py do.py smoke`

---

## Acceptance Criteria

- [ ] A malformed msgpack packet does not kill the reader thread; it is logged and skipped.
- [ ] Graceful disconnection (not a hang) when the reader thread exits due to a fatal error.
- [ ] RPC unit tests pass.

---

## Interdependencies

- **WI 100** (rpc-callbacks-init-order-race) — same file; fix together.
- **WI 89** (rpc-notification-callback-under-lock) — also modifies `rpc.cpp`; batch in one pass.
