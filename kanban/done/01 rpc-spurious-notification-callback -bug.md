# rpc.cpp: spurious on_notification_available on write failure

**Severity:** HIGH  
**File:** `libs/draxul-nvim/src/rpc.cpp:269–276`  
**Source:** review-bugs-consensus BUG-02 (claude)

## Bug Description

The write-failure path in `NvimRpc::notify()` fires `callbacks_.on_notification_available` even though no notification was pushed and this is not a reader-thread error. The contract documented in `nvim_rpc.h:188` says the callback fires when a notification is pushed *or* when the reader thread detects a fatal pipe error. Neither condition is met here.

The main loop (awakened by `wake_window()`) calls `drain_notifications()`, gets an empty result, and loops — once per keystroke or scroll event that triggered a `notify()` during the failure window. This can cause a tight wake-up loop.

**Trigger:** Any write to nvim's stdin fails (broken pipe, child exited).

## Investigation

- [ ] Read `nvim_rpc.h:188` to confirm the contract
- [ ] Confirm `drain_notifications()` is the only consumer of the callback and that it handles empty results by returning without side effects
- [ ] Confirm the reader-thread error paths at `rpc.cpp:435–436` and `rpc.cpp:464–466` are the only legitimate callers of the callback
- [ ] Check whether `connection_failed()` is polled by the main loop each frame — if so, this is the correct way for the main loop to detect the failure

## Fix Strategy

- [ ] Remove lines 274–275 from `NvimRpc::notify()`:
  ```cpp
  // DELETE these two lines:
  if (callbacks_.on_notification_available)
      callbacks_.on_notification_available();
  ```
- [ ] Keep `impl_->read_failed_ = true` and `impl_->response_cv_.notify_all()` — these are correct
- [ ] Verify the main loop detects `connection_failed()` on the next pump without needing the spurious wake

## Acceptance Criteria

- [ ] Simulated write failure (e.g. close the pipe in a test) does not trigger `on_notification_available`
- [ ] `drain_notifications()` is not called spuriously after a write failure
- [ ] Any pending `request()` callers still unblock promptly via `response_cv_`
- [ ] `draxul-tests` passes; no regression in RPC transport tests
