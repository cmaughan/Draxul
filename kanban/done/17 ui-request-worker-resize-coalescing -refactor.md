# UiRequestWorker Resize Coalescing

**Type:** refactor
**Priority:** 17
**Raised by:** Claude

## Summary

Coalesce rapid resize events in `UiRequestWorker` so that only the most-recent dimensions are sent to Neovim, preventing queue back-pressure during fast window resizing where each resize would otherwise make a blocking `nvim_ui_try_resize` RPC call.

## Background

Under fast window resizing, the worker could accumulate pending resize events and issue one blocking 5-second-timeout RPC call per resize, causing the queue to back up and the UI to feel sluggish or unresponsive. The fix is to discard intermediate sizes and only send the most recent dimensions.

## Implementation Plan

### Files modified
- `libs/draxul-app-support/src/ui_request_worker_state.h` — `UiRequestWorkerState` stores a single `optional<PendingResizeRequest>` instead of a queue; each `request_resize` call overwrites the previous pending request, providing inherent coalescing.
- `libs/draxul-app-support/src/ui_request_worker.cpp` — `thread_main` takes the pending request under the lock, releases the lock, makes the blocking RPC call, then loops back to wait; any new resize that arrived during the RPC call is picked up on the next iteration.
- `tests/ui_request_worker_tests.cpp` — tests verifying coalescing behaviour, clean shutdown with an in-flight request, and post-stop request rejection.

### Steps
- [x] Replace any queue-based pending storage with a single `optional<PendingResizeRequest>` in `UiRequestWorkerState`
- [x] Implement `request_resize` to overwrite the optional (coalescing) and return false when not running
- [x] Implement `take_pending_request` to atomically move out the optional under the caller's lock
- [x] Ensure thread safety: all access to state_ protected by mutex_ in `UiRequestWorker`
- [x] Add tests: burst coalescing keeps the latest dimensions; stop with in-flight request; post-stop requests ignored
- [x] Build: `cmake --build build --target draxul draxul-tests --parallel`
- [x] Tests: `./build/tests/draxul-tests` — all ok
- [x] Smoke: `./build/draxul --smoke-test` — exit 0

## Depends On
- Work item 25 (ui_request_worker moved to draxul-app-support)

## Blocks
- None

## Notes
The implementation uses a single optional rather than a queue, so coalescing is O(1) and requires no queue scanning. The mutex in `UiRequestWorker` protects all accesses to `state_` from both the main thread (enqueue) and the worker thread (dequeue/process).

> Work item produced by: claude-sonnet-4-6
