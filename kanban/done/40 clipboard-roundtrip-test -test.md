# 40 Clipboard Round-Trip Test

## Why This Exists

Clipboard is the most user-visible cross-process feature in Draxul. It is implemented via fragile Lua injection (`g:clipboard` override via `nvim_exec_lua`) with zero automated test coverage. Any regression in the clipboard provider setup, `clipboard_set` notification handling, or `clipboard_get` request handling is invisible until a user reports it.

Identified by: **Claude** (tests #1, bad things #6), **GPT** (tests #9), **Gemini** (tests #9).

## Goal

Add unit/integration tests for the clipboard provider that mock the RPC channel and verify the full round-trip: provider setup → `clipboard_set` notification → `clipboard_get` request → text restored.

## Implementation Plan

- [x] Read `app/app.cpp` for `setup_clipboard_provider()` and the `handle_rpc_request("clipboard_get")` / `"clipboard_set"` handlers.
- [x] Read existing test files in `tests/` for patterns using the fake RPC channel (`IRpcChannel` mock or equivalent).
- [x] Write `tests/clipboard_tests.cpp`:
  - Test: `clipboard_set` notification stores text in the clipboard provider's internal buffer.
  - Test: `clipboard_get` request reads back text from the buffer and returns it over RPC.
  - Test: multi-line clipboard text (split on `\n`) round-trips correctly.
  - Test: empty clipboard returns empty array.
  - Test: clipboard_get before any clipboard_set returns a sensible default.
- [x] Wire the new test file into `tests/CMakeLists.txt`.
- [x] Run `ctest` and `clang-format`.

## Sub-Agent Split

Single agent. New test file only; no production code changes required.
