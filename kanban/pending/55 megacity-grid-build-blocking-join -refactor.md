# WI 55 — megacity-grid-build-blocking-join

**Type**: refactor  
**Priority**: 7 (UI-thread stall in performance-sensitive path)  
**Source**: review-consensus.md §R1 [P][G][C]  
**Produced by**: claude-sonnet-4-6

---

## Problem

`MegaCityHost::launch_grid_build()` (`libs/draxul-megacity/src/megacity_host.cpp:1341`) calls `grid_thread_.join()` on the main thread before spawning the next build. If the previous grid build is slow, this blocks `pump()` and the entire UI.

The pattern — join previous then start new — is safe but wrong for interactivity. The correct model is: cancel/supersede stale work, not block waiting for it.

---

## Tasks

- [ ] Read `libs/draxul-megacity/src/megacity_host.cpp` — understand the full lifecycle of `grid_thread_` and `launch_grid_build()`: how it is started, how the thread signals completion, and where `join()` is called.
- [ ] Read `libs/draxul-megacity/include/draxul/megacity_host.h` — check the data members used by the build thread.
- [ ] Design a cancellation approach: add an `std::atomic<bool> cancel_build_` flag that the build thread checks periodically. When a new build is requested while one is running: set the cancel flag, detach or let the old thread self-terminate, then start the new one.
- [ ] If the build thread holds shared state that is unsafe to abandon: use a generation counter (`std::atomic<int> build_gen_`) so the thread's result is discarded if the generation no longer matches when it tries to commit.
- [ ] Remove the synchronous `join()` from any path called from `pump()`. A blocking join on destruction (in the destructor) is acceptable.
- [ ] Ensure the destructor still joins cleanly so the thread does not outlive the host.
- [ ] Build: `cmake --build build --target draxul draxul-tests`
- [ ] Run smoke test and MegaCity test if available: `py do.py smoke`

---

## Acceptance Criteria

- Calling `launch_grid_build()` while a build is in progress does not block `pump()`.
- The destructor waits for the thread to finish (no detached threads at shutdown).
- Smoke test passes.
- No new data races (run under ASan: `cmake --preset mac-asan && cmake --build build --target draxul`).

---

## Interdependencies

- Independent of all other WI 48–60 items.
- If `DRAXUL_ENABLE_MEGACITY=OFF`, this code is compiled out; CI coverage requires MegaCity enabled.

---

## Notes for Agent

- A sub-agent is appropriate here: the MegaCity source files are large (~2293 lines). Brief the agent with the specific function and data member names from this description.
- Do not redesign the build pipeline; only remove the main-thread blocking join.
- Prefer `std::atomic<bool>` over a full `std::stop_token` (C++20 jthread) to keep the change minimal.
