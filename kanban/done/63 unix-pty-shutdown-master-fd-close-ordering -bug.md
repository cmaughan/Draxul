# Bug: UnixPtyProcess::shutdown() closes master_fd_ before joining reader thread

**Severity**: MEDIUM
**File**: `libs/draxul-host/src/unix_pty_process.cpp:181–188`
**Source**: review-bugs-consensus.md (M5)

## Description

`shutdown()` closes `master_fd_` at line 183, then joins `reader_thread_` at line 188. The reader thread is concurrently blocked in `poll()` (or between `poll()` and `read()`) on `master_fd_`. Closing a file descriptor that another thread is actively polling is not safe on all platforms:

- The closed fd number may be immediately reused by another `open()`/`socket()` in the process, causing the reader to inadvertently poll the new, unrelated fd.
- On some Linux kernels, `poll` on a closed fd returns `POLLNVAL` on the *next* iteration rather than unblocking the current one, introducing a race window.

The correct order is: signal the reader to stop → join the reader → then close fds.

## Trigger Scenario

Normal app exit or reconnect whenever a PTY terminal host is running.

## Fix Strategy

- [x] Signal the shutdown pipe before closing `master_fd_`:
  ```cpp
  // Signal reader thread first
  reader_running_ = false;
  if (shutdown_pipe_[1] >= 0)
      (void)::write(shutdown_pipe_[1], "x", 1);
  // Wait for reader to exit
  if (reader_thread_.joinable())
      reader_thread_.join();
  // Now safe to close fds
  if (master_fd_ >= 0) { close(master_fd_); master_fd_ = -1; }
  ```
- [x] Ensure `request_close()` is not called redundantly before `shutdown()` in the same lifecycle (or that its close of `master_fd_` is idempotent with the new ordering)
- [x] Fix H7 (poll EINTR) and H8 (write EINTR) in the same pass — see work items 58 and 59

## Acceptance Criteria

- [x] Under TSan, shutdown of a PTY process reports no data races on `master_fd_`
- [x] Reader thread exits cleanly on the shutdown pipe signal without needing `master_fd_` to close first
