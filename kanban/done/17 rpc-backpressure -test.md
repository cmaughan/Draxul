# NvimRpc Notification Queue Backpressure Test

**Type:** test
**Priority:** 17
**Raised by:** Claude, Gemini

## Summary

Add a test that enqueues 10,000 notifications into the `NvimRpc` notification queue from a producer thread and drains them from a consumer thread, verifying ordered delivery, no deadlock, and no dropped notifications.

## Background

The RPC reader thread pushes decoded notifications onto a thread-safe queue; the main thread drains it each frame. Under sustained high-frequency RPC traffic (e.g., a large neovim redraw burst), the queue may grow without bound if the main thread cannot drain it as fast as notifications arrive. The test should verify that the queue handles this backpressure scenario correctly: all notifications are eventually delivered in order, no notification is lost, and the system does not deadlock.

## Implementation Plan

### Files to modify
- `tests/rpc_backpressure_tests.cpp` — created

### Steps
- [x] Write test: single producer enqueues 10,000 notifications; single consumer drains all; verify count and order
- [x] Write test: producer enqueues 10,000 notifications faster than consumer drains (consumer sleeps 1µs per item); verify all are eventually delivered, no deadlock within 10 seconds
- [x] Write test: producer and consumer run concurrently; verify no notification is duplicated or dropped
- [x] Write test: producer enqueues notifications, consumer calls drain once after all are enqueued; verify all notifications received in enqueue order
- [x] Register with ctest (added to draxul-tests in tests/CMakeLists.txt)

## Depends On
- `24 rpc-buffer-linear-copy -refactor.md` — easier to test after the buffer fix, but can be written against current code

## Blocks
- None

## Notes
Use `std::thread` for the producer in the concurrent tests. Set a test timeout to ensure deadlock is detected within a reasonable time. The queue implementation should be in `libs/draxul-nvim/src/`; identify the exact class before writing the test.

> Work item produced by: claude-sonnet-4-6
