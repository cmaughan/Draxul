# WI 11 — `ToastHost::initialize()` dereferences possibly-null grid handle

**Type:** bug  
**Severity:** HIGH  
**Source:** review-bugs-latest.gpt.md  
**Consensus:** review-consensus.md Phase 2

---

## Problem

`app/toast_host.cpp:25–30`: `ToastHost::initialize()` calls `create_grid_handle()` and **immediately uses `handle_`** without checking whether it is null.

Every other overlay host in the codebase (`GridHostBase`, `ChromeHost`, `CommandPaletteHost`) guards this call:

```cpp
handle_ = create_grid_handle();
if (!handle_) {
    return false;  // or log + return false
}
```

`ToastHost` is missing this guard. If `create_grid_handle()` fails (e.g. renderer not yet attached, GPU resource exhaustion), the very next line dereferences a null pointer and crashes.

This is an outlier in an otherwise consistent pattern, making it a genuine crash surface rather than a theoretical concern.

**Files:**
- `app/toast_host.cpp` (~lines 25–30)

---

## Implementation Plan

- [x] Add a null check after `create_grid_handle()` in `ToastHost::initialize()`:
  ```cpp
  handle_ = create_grid_handle();
  if (!handle_) {
      DRAXUL_LOG_ERROR(LogCategory::App, "ToastHost: failed to create grid handle");
      return false;
  }
  ```
- [x] Verify this matches the pattern in `ChromeHost::initialize()` for consistency.
- [x] Add a unit test (see WI 19) that injects a null `create_grid_handle` return and asserts graceful `false` return, not a crash.
- [x] Run under ASan to confirm no use-after-null in the rest of the `initialize()` path.

---

## Interdependencies

- Should be fixed before WI 19 (ToastHost init-failure test) which tests exactly this path.
- WI 12 (toast idle wake gap) is a separate bug in the same file; fix both in the same PR.
- WI 18 (toast idle wake delivery test) can be written once both bugs are fixed.

---

*Filed by: claude-sonnet-4-6 — 2026-04-08*
