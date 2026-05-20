# 43 Nvim Crash Lifecycle Test

## Why This Exists

If the nvim child process is force-killed or crashes, Draxul should display a clean diagnostic (or at least not crash itself). Currently there is no test for this scenario. The `NvimProcess` and `NvimRpc` shutdown paths both depend on careful ordering that is only verified manually.

Identified by: **Gemini** (tests #10), **GPT** (tests #10).

## Goal

Add a test that spawns a real nvim process, force-kills it, and verifies Draxul's reader thread and RPC layer shut down cleanly without deadlock or crash, and that the expected log warning is emitted.

## Implementation Plan

- [x] Read `libs/draxul-nvim/include/draxul/nvim.h` and `libs/draxul-nvim/src/` for `NvimProcess` and `NvimRpc` APIs.
- [x] Read `tests/nvim_process_tests.cpp` and `tests/rpc_integration_tests.cpp` for existing patterns.
- [x] Write `tests/nvim_crash_tests.cpp`:
  - Start a real nvim process via `NvimProcess`.
  - Establish an RPC connection.
  - Force-kill the nvim process (SIGKILL / TerminateProcess).
  - Assert `NvimRpc::close()` returns within a timeout (e.g., 2 seconds).
  - Assert the reader thread has exited (join with timeout).
  - Assert the log contains the expected "process exited" or "pipe closed" warning.
  - Assert the calling thread does not deadlock.
- [x] Wire into `tests/CMakeLists.txt`.
- [x] Run `ctest` and `clang-format`.

## Sub-Agent Split

Single agent. Extends existing nvim process test patterns.
