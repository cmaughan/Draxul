# 39 Synchronous RPC Blocks UI Thread

## Why This Exists

`NvimRpc::request()` blocks the calling thread for up to 5 seconds waiting for a response. This is called from the main thread during clipboard and resize operations. A slow or unresponsive nvim process stalls the render loop for the full timeout, producing a frozen window that users interpret as a crash.

Identified by: **GPT** (bad things #8). Noted by: **Claude** as a threading model concern.

## Goal

Prevent synchronous RPC `request()` calls from blocking the render thread. Short term: add a compile-time assertion or runtime check that `request()` is not called from the main thread. Long term: convert clipboard and resize RPCs to async callbacks.

## Implementation Plan

- [x] Read `libs/draxul-nvim/include/draxul/nvim.h` and `libs/draxul-nvim/src/nvim_rpc.cpp` to understand the `request()` API and its callers.
- [x] Identify every call to `rpc_.request(...)` (or equivalent) in `app.cpp` and `ui_request_worker.cpp`.
- [x] Add a `std::thread::id g_main_thread_id` global set at startup, and `assert` inside `NvimRpc::request()` (guard is a no-op until `set_main_thread_id()` is called, so init-time calls are unaffected).
- [x] Verify existing callers: `UiRequestWorker` runs on a worker thread (OK). `attach_ui`, `execute_startup_commands`, `setup_clipboard_provider` run during init before guard is armed (OK). `dispatch_gui_action("copy")` and `paste_text()` ran on main thread during the run loop — fixed.
- [x] For main-thread callers: `paste_text()` converted from `request()` to `notify()`; copy action converted to async Lua `rpcnotify` via existing `clipboard_set` mechanism.
- [x] Add a test that calls `request()` from a non-main thread and verifies it completes correctly (uses the fake RPC server).
- [x] Run `ctest` and `clang-format` on touched files.

## Sub-Agent Split

Single agent. Changes to `nvim_rpc.cpp` and `app.cpp` clipboard path.
