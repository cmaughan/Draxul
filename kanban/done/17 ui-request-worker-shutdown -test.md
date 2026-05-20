# 17 UiRequestWorker Coalesce and Shutdown Test

## Why This Exists

`UiRequestWorker` offloads the blocking `nvim_ui_try_resize` call so the main thread never stalls waiting for Neovim to acknowledge a resize. It uses last-write-wins coalescing: if multiple resize requests arrive before the worker thread processes one, only the latest is sent.

There are no tests for:
- Rapid-burst coalescing: only the last resize is sent, not all of them.
- Clean shutdown: the worker exits without hanging when the app exits while a resize is in flight.
- Request loss semantics: if a resize is enqueued after `stop()` is called, it is silently dropped (not panicked or deadlocked).

**Source:** `app/ui_request_worker.cpp`, `app/ui_request_worker.h`.
**Raised by:** GPT (primary), Claude.

## Goal

Add focused tests for `UiRequestWorker`:
1. Enqueue 10 resize requests in rapid succession → verify only 1 RPC call is made (the last value).
2. Start the worker, enqueue a request, call `stop()` → verify the thread joins cleanly within a bounded timeout.
3. Call `stop()` then enqueue a request → verify it is silently dropped and no crash/deadlock occurs.

## Implementation Plan

- [x] Read `app/ui_request_worker.cpp` and `app/ui_request_worker.h` to understand the API.
- [x] Read how the worker interacts with `IRpcChannel` to understand the mock seam available.
- [x] Create a mock `IRpcChannel` that counts calls and records the last resize dimensions sent.
- [x] Write tests:
  - `resize_worker_coalesces_burst_to_single_rpc`
  - `resize_worker_clean_shutdown`
  - `resize_worker_drop_after_stop`
- [x] Add tests to `draxul-tests` (new file `tests/ui_request_worker_tests.cpp`).
- [x] Run `ctest --test-dir build`.

## Notes

The test for shutdown should use a bounded `std::future::wait_for` or similar timeout so a hung
thread does not block CI indefinitely.

Implemented with a split seam:
- `UiRequestWorkerState` now has deterministic coalescing/drop tests.
- `UiRequestWorker` itself has bounded shutdown and post-stop drop coverage with a blocking fake RPC.

## Sub-Agent Split

Single agent. The mock RPC channel can reuse the existing `IRpcChannel` test double if one exists.
