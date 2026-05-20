# WI 31 — nvim-posix-fd-inheritance

**Type:** bug  
**Priority:** HIGH (resource leaks; FD exhaustion risk; potential deadlocks)  
**Platform:** macOS / Linux only  
**Source:** review-bugs-latest.gemini.md — CRITICAL finding  
**Produced by:** claude-sonnet-4-6

---

## Problem

`libs/draxul-nvim/src/nvim_process.cpp` (POSIX path) calls `fork()` and then `execvp()` to launch Neovim. The child process inherits **all open file descriptors** from the parent (Draxul) that do not have `O_CLOEXEC` / `FD_CLOEXEC` set. This includes:

- The log file handle (if `--log-file` is given)
- SDL internal FDs (e.g. GPU device FDs on macOS, epoll/kqueue FDs)
- Any network sockets opened by third-party libraries
- The `stdin`/`stdout` pipe ends for *other* hosts (if multiple panes are open)

Consequences:
1. Neovim and every shell command it spawns hold these FDs open. Tools that rely on pipe-end closure (e.g., `yes | head -n1`) may stall because the write end is still alive in a child.
2. If a log file is inherited, it stays open after Draxul closes it — preventing log rotation.
3. On systems with low FD limits, long-running sessions accumulate leaked FDs from fork churn.

Note: WI 90 (complete) fixed SIGPIPE disposition inheritance but did not address FD inheritance.

---

## Investigation

- [x] Read `libs/draxul-nvim/src/nvim_process.cpp` POSIX section (`#ifndef _WIN32`): identify the fork/exec block and what FDs are explicitly closed in the child before execvp.
- [x] Check `libs/draxul-host/src/unix_pty_process.cpp`: same pattern exists there; this WI should cover both.
- [x] Identify which FDs are already set with `O_CLOEXEC` (pipe creation, etc.) and which are not.
- [x] Check SDL3 documentation / source for whether SDL opens FDs with `O_CLOEXEC` by default on Apple platforms.

---

## Fix Strategy

Two complementary approaches — apply both:

### Option A: Set `O_CLOEXEC` at creation time (preferred, zero-cost at fork)
- [ ] Audit all `open()` / `pipe()` / `socket()` calls in Draxul's POSIX paths. Prefer `pipe2(fds, O_CLOEXEC)` / `open(..., O_CLOEXEC)` / `socket(..., SOCK_CLOEXEC)` where available (macOS 10.9+, Linux 2.6.27+).
- [ ] For FDs created by third-party libraries (SDL, FreeType), apply `fcntl(fd, F_SETFD, FD_CLOEXEC)` immediately after creation where identifiable.

### Option B: Close-all above STDERR in the child after fork, before exec
- [x] In the child block of `nvim_process.cpp` (and `unix_pty_process.cpp`), after duplicating the pipe ends to stdin/stdout, add a loop:
  ```cpp
  // macOS: use closefrom(3) if available (macOS 10.12+)
  // Fallback: iterate /proc/self/fd or use getdtablesize()
  int maxfd = getdtablesize();
  for (int fd = STDERR_FILENO + 1; fd < maxfd; ++fd) {
      if (fd != child_stdin_fd && fd != child_stdout_fd)
          close(fd);
  }
  ```
  On macOS, prefer `closefrom(STDERR_FILENO + 1)` (available since macOS 10.12) after explicitly keeping the desired pipe FDs.

### Testing
- [x] After the fix: fork a child that writes its open FDs (e.g., reads `/proc/self/fd` on Linux or uses `F_GETFD` loop) and verify only stdin/stdout/stderr are open.
- [x] Run under ASan + TSan to confirm no file-table races.
- [ ] Manual smoke: `yes | head -n1` in a Draxul shell pane should terminate promptly.

---

## Acceptance Criteria

- [x] Child process (nvim, shell) has no unexpected inherited FDs beyond stdin/stdout/stderr.
- [ ] `yes | head -n1` terminates in < 100ms in a Draxul shell pane.
- [x] Smoke test passes: `cmake --build build --target draxul draxul-tests && py do.py smoke`
- [x] No new ASan/TSan findings from the changes.

---

## Notes

- Windows path is not affected (Windows uses explicit `bInheritHandle` flags per HANDLE).
- `unix_pty_process.cpp` has the same structural fork/exec pattern and should be patched in the same commit.
- A sub-agent can do this work independently; no other WIs block it.
