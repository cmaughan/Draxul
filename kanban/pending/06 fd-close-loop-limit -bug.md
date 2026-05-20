# fork() child: FD close-loop fallback misses high-numbered FDs

**Severity:** MEDIUM  
**Files:** `libs/draxul-nvim/src/nvim_process.cpp:328–336`, `libs/draxul-host/src/unix_pty_process.cpp:95–98`  
**Source:** review-bugs-consensus BUG-07 (claude)

## Bug Description

Both fork-child paths close inherited FDs via a loop from `STDERR_FILENO + 1` up to a computed limit:

```cpp
const int max_fd = static_cast<int>(sysconf(_SC_OPEN_MAX));
const int limit = (max_fd > 0) ? max_fd : 1024;
for (int fd = STDERR_FILENO + 1; fd < limit; ++fd) { close(fd); }
```

If `sysconf(_SC_OPEN_MAX)` returns `−1` (valid on some configurations), the fallback limit of 1024 will miss any FD opened by SDL, Metal, font libraries, or third-party SDKs with numbers ≥ 1024, leaking them into the nvim child or shell child.

**Trigger:** Any library in the parent process opens a file descriptor numbered ≥ 1024 before the first `spawn()` call, combined with a system where `sysconf(_SC_OPEN_MAX)` returns `−1`.

## Investigation

- [ ] Check how SDL3 and Metal/Vulkan manage FDs — confirm whether any routinely exceed 1023
- [ ] Verify `sysconf(_SC_OPEN_MAX)` returns a sensible value on current macOS versions (should be 2560+)
- [ ] Confirm `closefrom(3)` availability on macOS 10.12+ (it is available)
- [ ] Check the exec-status pipe FD value in `nvim_process.cpp` — must be preserved across the close

## Fix Strategy

- [ ] Replace the loop in both files with `closefrom(STDERR_FILENO + 1)` on macOS:
  ```cpp
  #ifdef __APPLE__
  // Re-open exec_status_pipe[1] won't work; skip it during closefrom, then
  // closefrom closes everything else atomically.
  // closefrom skips the exec_status_pipe[1] FD by duplicating to a known slot
  // OR: close all except the one needed FD manually
  closefrom(STDERR_FILENO + 1);   // closes all FDs >= 4
  // Re-dup exec_status_pipe[1] to a low slot if needed, or just skip for unix_pty
  #else
  // existing loop
  #endif
  ```
  For `nvim_process.cpp`: save `exec_status_pipe[1]` to a known low slot (`dup2` to e.g. fd 3), then call `closefrom(4)`, then restore.
- [ ] For `unix_pty_process.cpp` (no exec-status pipe): `closefrom(STDERR_FILENO + 1)` with no special handling needed.
- [ ] Keep the loop as a fallback for Linux until `close_range()` support is confirmed.

## Acceptance Criteria

- [ ] After the fix, no FDs > 2 are open in the spawned nvim or shell child (verify with `/proc/self/fd` or `lsof` in a test)
- [ ] `sysconf(_SC_OPEN_MAX)` returning `−1` no longer results in a 1024-only sweep
- [ ] Smoke test passes; `mac-asan` build shows no FD-related errors
- [ ] Windows path is unaffected (this is macOS/Linux only)
