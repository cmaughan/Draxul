# 03 nvim-rpc-drain-concurrent — Test

## Summary

`NvimRpc::drain_notifications()` swaps the internal notification vector out under a mutex. The reader thread appends notifications under the same mutex. The current test suite exercises the queue in single-threaded scenarios only. A concurrent stress test is needed to:

1. Verify no notification is lost or double-delivered when the reader and drain threads run simultaneously.
2. Confirm no data races exist (the test should be run under AddressSanitizer + ThreadSanitizer).
3. Document the capacity behavior: whether the queue grows unboundedly under sustained load (see also icebox rpc-notification-backlog).

## Steps

- [ ] 1. Read `libs/draxul-nvim/include/draxul/nvim.h` to find the `NvimRpc` class interface, specifically the notification queue type, `drain_notifications()` signature, and any method for pushing synthetic notifications.
- [ ] 2. Read `libs/draxul-nvim/src/nvim_rpc.cpp` (or equivalent) to understand the internal queue, mutex, and swap implementation.
- [ ] 3. Read `tests/CMakeLists.txt` to understand how test files are registered, and `tests/test_main.cpp` (or equivalent) to see the test registration pattern.
- [ ] 4. Create `tests/nvim_rpc_concurrent_tests.cpp`. The test structure:
  ```cpp
  // Test: no notifications lost under concurrent push/drain
  // 1. Construct or access the NvimRpc notification queue component.
  //    If NvimRpc cannot be constructed without spawning a real nvim process,
  //    extract the queue into a testable helper class or test the queue type
  //    (e.g., draxul::NotificationQueue) directly.
  // 2. Spawn a producer thread that pushes N=10,000 synthetic notification
  //    objects (any valid msgpack payload that passes the push API).
  // 3. On the main thread, call drain_notifications() in a tight loop until
  //    total_received >= N.
  // 4. Assert total_received == N.
  // 5. Assert no notification appears twice (use a unique ID field or counter).
  // 6. No crashes, no assertions, no sanitizer errors.
  ```
- [ ] 5. If `NvimRpc` is not constructable in tests without a live process, identify the smallest extractable unit (the mutex+vector queue) and test that directly. If extraction is needed, note it as a follow-up in `15 handle-csi-dispatch-table` (architectural), not this item.
- [ ] 6. Add a second test: producer pushes 10,000 notifications in 10 batches of 1,000 with brief `std::this_thread::yield()` between batches; drain runs concurrently. Same assertion: total == 10,000.
- [ ] 7. Add a third test: drain is called when the queue is empty; assert it returns an empty container and does not block.
- [ ] 8. Register the new test file in `tests/CMakeLists.txt` (add to the source list for the `draxul-tests` target).
- [ ] 9. Register the test cases in `tests/test_main.cpp` if manual registration is used (check the existing pattern).
- [ ] 10. Build with the ASan preset: `cmake --preset mac-asan && cmake --build build --target draxul-tests`.
- [ ] 11. Run: `ctest --test-dir build -R draxul-tests`. All tests must pass under ASan.
- [ ] 12. If ThreadSanitizer is available (separate CMake preset), also run under TSan.
- [ ] 13. Run clang-format on the new test file.
- [ ] 14. Mark this work item complete and move to `plans/work-items-complete/`.

## Acceptance Criteria

- A stress test exists that pushes 10,000 notifications from a background thread while `drain_notifications()` runs on the main thread.
- The test asserts zero lost and zero duplicate notifications.
- The test passes under AddressSanitizer (and ThreadSanitizer if available).
- Empty-queue drain returns cleanly.

*Authored by: claude-sonnet-4-6*
