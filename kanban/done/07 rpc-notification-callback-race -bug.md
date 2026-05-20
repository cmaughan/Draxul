# WI 07 — `on_notification_available` / `on_request` assigned after reader thread starts

**Type:** bug  
**Severity:** HIGH  
**Source:** review-bugs-latest.gemini.md (CRITICAL), review-bugs-latest.gpt.md (HIGH)  
**Consensus:** review-consensus.md Phase 1

---

## Problem

In `NvimHost::initialize_host()`, the RPC reader thread is started via `rpc_.initialize()` **before** `on_notification_available` and `on_request` `std::function` members are assigned. These members are `public` in `NvimRpc` (`nvim_rpc.h:95`).

If Neovim sends a notification or request immediately upon startup (e.g. during UI attach negotiation), the reader thread reads a partially-initialised `std::function`, causing undefined behaviour (likely a null-function call crash or data race).

`std::function` is not thread-safe for concurrent assignment vs. invocation. Even if the window is small in practice, this is a genuine data race that TSan will flag.

**Files:**
- `libs/draxul-nvim/src/rpc.cpp:58` (reader thread start)
- `libs/draxul-host/src/nvim_host.cpp` (callback assignment location)
- `libs/draxul-nvim/include/draxul/nvim_rpc.h` (~line 95)

---

## Implementation Plan

- [x] Option B (chosen): `NvimRpc::initialize()` now accepts a `RpcCallbacks` struct by value and stores it before the reader thread is spawned. `std::thread` construction provides the happens-before edge, so the reader never observes a default-constructed `std::function`.
- [x] Made `on_notification_available` / `on_request` private (moved into the new `RpcCallbacks` aggregate, held as `NvimRpc::callbacks_`). Public assignment is no longer possible — callbacks can only be supplied at `initialize()` time.
- [x] Updated `NvimHost::initialize_host()` to build an `RpcCallbacks` and hand it to `rpc_.initialize(nvim_process_, std::move(rpc_callbacks))`.
- [x] Added regression test `rpc WI07: callbacks supplied at initialize() fire for immediate notifications` in `tests/rpc_backpressure_tests.cpp`, plus updated the existing `on_notification_available` test to use the new API.
- [x] Verified with `cmake --build build --target draxul draxul-tests` and `ctest --test-dir build -R draxul-tests` (all pass). `py do.py smoke` also passes.

---

## Interdependencies

- Batch with WI 04, 05, 06 (Phase 1 RPC hardening) — all in `rpc.cpp` / `nvim_host.cpp`.
- WI 112 (tsan-build-preset, active) provides the TSan infrastructure needed to validate this fix.

---

*Filed by: claude-sonnet-4-6 — 2026-04-08*
