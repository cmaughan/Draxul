# Bug: PTY write returns false on EINTR, silently dropping input

**Severity**: HIGH
**File**: `libs/draxul-host/src/unix_pty_process.cpp:244`
**Source**: review-bugs-consensus.md (H8)

## Description

`UnixPtyProcess::write` loops over a buffer writing to the PTY master fd. At line 244:
```cpp
const ssize_t written = ::write(master_fd_, ptr, remaining);
if (written <= 0)
    return false;
```
If `::write()` is interrupted by a signal (`errno == EINTR`), it returns `-1`. The code treats this as an unrecoverable error and returns `false`, losing the current keypress or paste payload. The caller has no way to retry since the write position has already advanced (it hasn't — `ptr` and `remaining` are unchanged, but the caller discards the result).

## Trigger Scenario

Signal delivered during a large paste operation or keypress write to the PTY master fd.

## Fix Strategy

- [x] After `written < 0`, check errno and retry on EINTR:
  ```cpp
  if (written < 0)
  {
      if (errno == EINTR)
          continue;
      return false; // real write error
  }
  if (written == 0)
      return false; // unexpected: write to PTY returned 0
  ```
- [x] Fix H7 (poll EINTR) and M5 (shutdown ordering) in the same file pass — see work items 58 and 63

## Acceptance Criteria

- [x] Pasting a large buffer while signals are being delivered does not lose any bytes
- [x] PTY write returns false only on genuine errors, not on EINTR
