# Bug: PTY reader thread exits permanently on EINTR from poll()

**Severity**: HIGH
**File**: `libs/draxul-host/src/unix_pty_process.cpp:272`
**Source**: review-bugs-consensus.md (H7)

## Description

The reader loop at line 272:
```cpp
if (const int ret = poll(fds, 2, -1); ret <= 0)
    break;
```
`poll()` can return `-1` with `errno == EINTR` when a signal interrupts it (e.g., `SIGWINCH` on window resize, `SIGCHLD` when a child process changes state). The current code treats any non-positive result as a fatal error and exits the reader thread permanently. All future output from the PTY (Bash, Zsh, etc.) is silenced until the process is restarted.

## Trigger Scenario

Any signal delivered to the process while a PTY terminal host is active. On macOS and Linux, resizing the window sends `SIGWINCH`, so this fires reliably when the user resizes the Draxul window.

## Fix Strategy

- [x] After the `poll()` call, handle EINTR explicitly:
  ```cpp
  const int ret = poll(fds, 2, -1);
  if (ret < 0)
  {
      if (errno == EINTR)
          continue;
      break; // real error
  }
  if (ret == 0)
      continue; // timeout (shouldn't happen with -1 timeout)
  ```
- [x] Fix H8 (write EINTR) and M5 (shutdown ordering) in the same file pass — see work items 59 and 63

## Acceptance Criteria

- [x] Resizing the Draxul window while a PTY terminal is running does not kill the reader thread
- [x] Terminal output continues normally after a resize
- [x] Sending explicit SIGWINCH to the process does not silence the terminal
