# try_get_int: uncaught exception + signed overflow UB

**Severity:** CRITICAL  
**File:** `libs/draxul-nvim/src/ui_events.cpp:49–55`  
**Source:** review-bugs-consensus BUG-01 (claude)

## Bug Description

`try_get_int()` has two compounding defects on a single line:

1. `as_int()` throws `std::range_error` when a `UInt` variant exceeds `INT64_MAX`. No `try/catch` exists on the `pump_once → process_redraw` call path, so the exception reaches `std::terminate`.
2. The cast `(int)value.as_int()` narrows `int64_t → int`. Any value outside `[INT_MIN, INT_MAX]` is signed integer overflow — UB in C++. The caller in `handle_hl_attr_define` widens back to `int64_t` too late.

**Trigger:** Any msgpack `uint64` or `int64` field exceeding `INT_MAX` in a grid-redraw event (e.g. large attr IDs from a plugin, or a malformed nvim process).

## Investigation

- [ ] Confirm `as_int()` throws `std::range_error` for `UInt > INT64_MAX` via `nvim_rpc.h:108–109`
- [ ] Verify no `try/catch` on the `pump_once → walk_pump → host::pump → process_redraw` path
- [ ] Check all callers of `try_get_int` — confirm all consume the `out` value without further range-checking
- [ ] Confirm the reader-thread catch at `rpc.cpp:521` does NOT protect the main thread

## Fix Strategy

- [ ] Add range check and exception guard inside `try_get_int`:
  ```cpp
  try {
      const int64_t v = value.as_int();
      if (v < INT_MIN || v > INT_MAX) return false;
      out = static_cast<int>(v);
  } catch (const std::exception&) {
      return false;
  }
  ```
- [ ] Remove the raw `(int)` C-style cast entirely
- [ ] Do NOT add a try/catch at the call sites; the fix belongs in the helper

## Acceptance Criteria

- [ ] `try_get_int` with a `UInt` value of `UINT64_MAX` returns `false` without throwing
- [ ] `try_get_int` with an `int64` value of `INT64_MAX` (> `INT_MAX`) returns `false`
- [ ] `try_get_int` with an `int64` value of `42` still returns `true` with `out == 42`
- [ ] Build and run `draxul-tests`; all existing RPC/UI-event tests pass
- [ ] No `(int)` or `static_cast<int>` from `int64_t` without a range check in this file
