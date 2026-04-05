# WI 88 — pty-pollhup-data-loss

**Type:** bug  
**Priority:** 1 (data loss — last output chunk silently dropped)  
**Source:** review-bugs-consensus.md §H2 [Gemini]  
**Produced by:** claude-sonnet-4-6

---

## Problem

In `libs/draxul-host/src/unix_pty_process.cpp:299–315`, the reader loop checks `POLLHUP | POLLERR` at line 300 and breaks *before* checking `POLLIN` at line 303. When a process exits with unread data still in the kernel PTY buffer, `poll()` sets both `POLLIN` and `POLLHUP` simultaneously. The current code breaks on `POLLHUP` and discards the unread data. In practice, the last few lines of every command's output before the shell prompt are silently lost.

---

## Investigation

- [ ] Read `libs/draxul-host/src/unix_pty_process.cpp:275–325` — confirm the exact order of `POLLHUP` and `POLLIN` checks and the break/continue logic.
- [ ] Verify there is no equivalent bug in `libs/draxul-host/src/conpty_process.cpp` (Windows reader thread).

---

## Fix Strategy

- [ ] Reorder the conditional: check `POLLIN` first and read all available data; only break on `POLLHUP | POLLERR` when `POLLIN` is not set or `read()` returns 0/error:
  ```cpp
  if (fds[0].revents & POLLIN)
  {
      const ssize_t bytes_read = ::read(master_fd_, buffer.data(), buffer.size());
      if (bytes_read <= 0)
          break;
      // ... process data ...
  }
  else if (fds[0].revents & (POLLHUP | POLLERR))
      break;
  ```
- [ ] Build: `cmake --build build --target draxul draxul-tests`
- [ ] Run smoke test: `py do.py smoke`

---

## Acceptance Criteria

- [ ] Running `echo hello && sleep 0` in the terminal and exiting shows `hello` (and any other output) fully before the shell prompt.
- [ ] No data is discarded when `POLLHUP` and `POLLIN` arrive together.
- [ ] Smoke test passes.

---

## Interdependencies

- **WI 97** (nvim-write-return-unchecked) — both address the POSIX process/pipe error path; can be done in the same pass.
