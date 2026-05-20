# 45 Shutdown Race Test

## Why This Exists

The shutdown sequence — RPC close, resize worker stop, nvim process termination — involves three threads and careful ordering. The existing tests cover RPC close unblocking in-flight requests, but do not cover the race between a pending resize RPC and simultaneous process exit, or `shutdown()` being called while `request()` is mid-flight.

Identified by: **Claude** (tests #7), **GPT** (tests #6), **Gemini**.

## Goal

Add tests that exercise concurrent shutdown scenarios: resize worker blocked on RPC + `close()` called concurrently; `shutdown()` called while a `request()` is in flight; reader thread exits before worker thread joins.

## Implementation Plan

- [x] Read `tests/rpc_integration_tests.cpp` for existing shutdown test patterns.
- [x] Read `libs/draxul-nvim/src/nvim_rpc.cpp` and `ui_request_worker.cpp` for shutdown sequencing.
- [x] Write `tests/shutdown_race_tests.cpp`:
  - [x] Test: call `rpc.close()` concurrently with an in-flight `request()` — verify no deadlock and no UB.
  - [x] Test: start 8 concurrent `request()` calls, call `close()` from another thread — verify all return promptly.
  - [x] Test: `UiRequestWorker` with a blocked RPC call; call worker `stop()` — verify worker thread joins within timeout.
  - [x] Test: nvim process exits while resize worker has a pending request — verify the request returns an error, not a hang.
  - [x] Test: `rpc.shutdown()` called while `request()` is mid-flight — clean return, no crash.
- [x] Wire into `tests/CMakeLists.txt`.
- [x] Use `std::latch` (C++20) for thread synchronisation in tests.
- [x] Run `ctest` and `clang-format`.

## Sub-Agent Split

Single agent. Potentially run with `-fsanitize=thread` in a separate CI step.
