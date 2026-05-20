# 39 background-worker-template -refactor

*Filed by: claude-sonnet-4-6 — 2026-03-29*
*Source: review-latest.claude.md [C]*

## Problem

Four components independently implement the same `thread + mutex + condition_variable +
stop_flag + optional<Result>` pattern:

- `libs/draxul-nvim/src/rpc.cpp` — reader thread
- `libs/draxul-host/src/unix_pty_process.cpp` (or equivalent) — PTY reader thread
- `libs/draxul-megacity/src/megacity_host.cpp` — `route_thread_`
- `libs/draxul-megacity/src/megacity_host.cpp` — `grid_thread_`

Each has its own shutdown logic.  Inconsistent shutdown ordering is a known source of
use-after-free in multi-threaded C++.  There is no central place to add instrumentation
(e.g., queue depth monitoring, thread-name assignment for profilers).

## Acceptance Criteria

- [ ] A `BackgroundWorker<Request, Result>` class template is defined in
      `libs/draxul-types/include/draxul/background_worker.h` (or
      `libs/draxul-app-support/include/draxul/background_worker.h`).
- [ ] The template encapsulates the mutex + CV + stop_flag + thread lifecycle.
- [ ] At least one existing component (suggested: NvimRpc reader) is migrated to use the
      template as a proof-of-concept.
- [ ] The remaining components are migrated or left with a TODO comment referencing this WI —
      not required all at once to keep blast radius manageable.
- [ ] Thread behaviour is identical to today (no performance regression).
- [ ] All existing tests pass.

## Implementation Plan

1. Read all four implementation sites to find the common pattern and any divergences.
2. Design `BackgroundWorker<Request, Result>` with:
   - `start(fn)` / `stop()` with a join-on-stop guarantee
   - `post(Request)` to enqueue work
   - `try_get_result()` to retrieve the latest result
   - Optional queue depth accessor for monitoring
3. Implement and unit-test the template in isolation (test: start, post, receive, stop).
4. Migrate `NvimRpc` reader as the proof-of-concept migration.
5. Run `cmake --build build --target draxul-tests && ctest`.

## Files Likely Touched

- `libs/draxul-types/include/draxul/background_worker.h` (new header-only template)
- `libs/draxul-nvim/src/rpc.cpp` (migration proof)
- `tests/background_worker_tests.cpp` (new unit tests for the template)

## Interdependencies

- **Independent of WI 38** (`App::Deps`) — can be done in parallel.
- Coordinate with WI 28 (rpc-timeout) — both touch `rpc.cpp`; do not combine in same commit.
- The MegaCity components can be migrated in a separate pass after the template is proven.
