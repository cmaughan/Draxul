# WI 03 — nvim-process-posix-eintr

**Type:** bug  
**Priority:** HIGH (spurious signal delivery tears down the Neovim RPC session)  
**Platform:** macOS / Linux only  
**Source:** review-bugs-consensus.md — BUG-04 (Claude + Gemini)

---

## Problem

`NvimProcess::write()` (POSIX path, `libs/draxul-nvim/src/nvim_process.cpp` line 422) tests `if (n <= 0) return false;` — this treats `errno == EINTR` (signal interrupted syscall) as a fatal write failure. `NvimProcess::read()` (line 432) tests `if (n < 0) return -1;` — same issue.

SDL installs signal handlers for SIGTERM, SIGINT, and others. Any handled signal delivered while the main thread is mid-`write()` or the reader thread is blocked in `read()` causes the syscall to return −1 with `errno == EINTR`. The `rpc.cpp` caller (line 187–193) then sets `impl_->read_failed_ = true`, notifies all waiters, and permanently tears down the RPC transport — crashing the Neovim session on a spurious signal.

The PTY sibling `unix_pty_process.cpp` handles EINTR correctly in both its read and write paths. `nvim_process.cpp` should match.

---

## Investigation

- [x] Read `libs/draxul-nvim/src/nvim_process.cpp` POSIX section (lines ~417–436) to confirm the missing EINTR handling in `write()` and `read()`.
- [x] Read `libs/draxul-host/src/unix_pty_process.cpp` write/read paths to confirm the correct EINTR pattern used there.
- [x] Read `libs/draxul-nvim/src/rpc.cpp` lines 180–195 to understand how `write()` returning false leads to `read_failed_ = true`.

---

## Fix Strategy

- [x] In `NvimProcess::write()` (POSIX), replace `if (n <= 0) return false;` with:
  ```cpp
  if (n < 0) {
      if (errno == EINTR) continue;
      return false;
  }
  if (n == 0) return false;
  ```
- [x] In `NvimProcess::read()` (POSIX), replace the single `::read()` call with a retry loop:
  ```cpp
  ssize_t n;
  do { n = ::read(impl_->child_stdout_read_, buffer, max_len); }
  while (n < 0 && errno == EINTR);
  if (n < 0) return -1;
  return (int)n;
  ```

---

## Acceptance Criteria

- [x] A signal delivered during nvim pipe I/O does not kill the RPC transport.
- [x] The fix matches the EINTR handling in `unix_pty_process.cpp`.
- [x] Build and smoke test pass: `cmake --build build --target draxul draxul-tests && py do.py smoke`.
- [ ] Run under TSan: `cmake --preset mac-tsan && cmake --build build --target draxul-tests && ctest --test-dir build -R draxul-tests`. No new findings.

---

## Notes

- Windows path uses `ReadFile`/`WriteFile` which do not return EINTR; Windows half of `nvim_process.cpp` is unaffected.
- Gemini also flagged `rpc.cpp:187–193` as a separate item. This is the same bug seen from the caller side; fixing `nvim_process.cpp` is sufficient and no independent `rpc.cpp` change is needed.
