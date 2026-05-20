# 03 rpc-backpressure-real-class — Test

## Summary

`tests/rpc_backpressure_tests.cpp` currently tests a locally-declared `NotificationQueue` rather than `NvimRpc`. The test comment explicitly acknowledges this. As a result, concurrency regressions, wakeup behavior changes, shutdown path errors, and storage-policy changes in the real `NvimRpc` class are completely invisible to this test suite. The test preserves the idea of a concurrent queue but does not guard the actual production code.

**Raised by:** GPT (review-latest.gpt.md)

## Steps

- [x] 1. Read `tests/rpc_backpressure_tests.cpp` — understand what the surrogate queue tests (push, drain, concurrent push+drain, shutdown).
- [x] 2. Read `libs/draxul-nvim/include/draxul/nvim_rpc.h` — find `NvimRpc`'s notification storage type, `drain_notifications()` method, and any mutex/condition-variable members.
- [x] 3. Read `libs/draxul-nvim/src/nvim_rpc.cpp` — understand the reader thread's push path and the main thread's drain path.
- [x] 4. Identify whether `NvimRpc` has an injectable seam for the notification queue (e.g. a `Deps` struct or template parameter). If not, determine the minimum change to add one for testing:
   - Option A: Extract the queue type into a testable header and test `NvimRpc` with a fake transport that pushes notifications.
   - Option B: Write an integration test that creates a real `NvimRpc` with a fake reader thread (using `rpc_fake_server.cpp` or equivalent) and drains from the main thread.
   - **Chosen: Option B** — added `notify_many` mode to `rpc_fake_server.cpp` (sends 100 sequential notifications then a response). No production code changes needed.
- [x] 5. Rewrite `tests/rpc_backpressure_tests.cpp` to test the real `NvimRpc` notification drain path:
   - Concurrent push (simulated reader thread via fake server) + drain (main thread) stress test.
   - Verify no messages are lost or duplicated.
   - Verify `drain_notifications()` returns empty when queue is empty.
   - Verify shutdown clears the queue correctly (drain after shutdown returns promptly).
- [x] 6. Keep any useful surrogate tests in a separate section or delete them if redundant. (Surrogate class removed entirely; all tests now use `NvimRpc` + fake server.)
- [x] 7. Build: `cmake --build build --target draxul-tests`.
- [x] 8. Run: `ctest --test-dir build -R draxul-tests`. All tests must pass.
- [x] 9. Run the new backpressure tests specifically under ASan: `cmake --preset mac-asan && cmake --build build --target draxul-tests && ctest --test-dir build -R draxul-tests`.
- [x] 10. Run clang-format on all touched files.
- [x] 11. Mark complete and move to `plans/work-items-complete/`.

## Acceptance Criteria

- `rpc_backpressure_tests.cpp` tests `NvimRpc` (or a direct seam into its notification queue), not a surrogate class.
- Concurrent push+drain stress test passes under ASan with no data races.
- Shutdown behavior is covered by at least one test.
- All existing tests continue to pass.

## Interdependencies

- Independent; does not depend on the unbounded-queue icebox item being resolved.
- The test should work with the current `NvimRpc` shape and will expose any real regressions.

*Authored by: claude-sonnet-4-6*
