# 04 rpc-main-thread-assert-release — Bug

## Problem

`NvimRpc::request()` used `assert()` to guard against being called from the main
thread. In Release builds (`NDEBUG` defined) the assert is compiled out, so a
main-thread call would silently block the render loop for up to the 5-second
request timeout.

## Fix

- [x] Add a runtime check directly after the `assert()` in `NvimRpc::request()` that survives Release builds.
- [x] Log an error via `DRAXUL_LOG_ERROR` and return an empty `RpcResult{}` immediately when the call originates from the main thread.
- [x] Keep the `assert()` for Debug builds (belt-and-suspenders).
- [x] Verified no other assert-only thread-safety guards exist in `rpc.cpp` or `nvim_process.cpp`.

## Files Changed

- `libs/draxul-nvim/src/rpc.cpp` — runtime guard added after the existing assert.
