# WI 09 — unix-pty-shutdown-main-thread-block

**Type:** bug  
**Priority:** MEDIUM (UI freeze on pane close when subprocess ignores SIGTERM)  
**Platform:** macOS / Linux only  
**Source:** review-bugs-consensus.md — BUG-10 (Gemini)

---

## Problem

`UnixPtyProcess::shutdown()` in `libs/draxul-host/src/unix_pty_process.cpp` (lines 121–186) is called on the main thread when a pane is closed. It runs two blocking wait loops:
- Up to 10 × 10 ms = **100 ms** for SIGTERM grace
- Up to 50 × 10 ms = **500 ms** for SIGKILL reap

Total potential block: **600 ms**. With a process in uninterruptible kernel sleep (hung NFS mount, active disk I/O), the entire GUI freezes for that duration. `NvimProcess::shutdown()` avoids this by moving the wait into a detached background thread; `UnixPtyProcess::shutdown()` should do the same.

---

## Investigation

- [x] Read `libs/draxul-host/src/unix_pty_process.cpp` lines 121–200 to confirm the blocking wait structure.
- [x] Read `libs/draxul-nvim/src/nvim_process.cpp` shutdown (POSIX path) to understand the detached-thread pattern to replicate.
- [x] Confirm which resources are safe to hand off to the background thread (pid, master_fd_, reader_thread_).
- [x] Check whether `reader_thread_.join()` (line 190) can also be moved to the background thread or if it must remain synchronous.

---

## Fix Strategy

- [x] Move the SIGTERM/SIGKILL wait loops into a `std::thread` that is detached immediately after launch:
  ```cpp
  // In shutdown(): signal the child, then detach the wait
  pid_t pid_copy = pid_;
  pid_ = -1;  // prevent double-kill
  std::thread([pid_copy, fg_pgid, ...]() {
      // existing SIGTERM + SIGKILL wait logic here
  }).detach();
  ```
- [x] Ensure the reader thread join on line 190 is still reachable (or moved into the detached thread after the wait).
- [x] Guard against double-shutdown (pid_ = -1 before detach).

---

## Acceptance Criteria

- [x] `UnixPtyProcess::shutdown()` returns within a few milliseconds on the main thread.
- [x] The child process is still reliably killed (SIGKILL escalation still runs in the background).
- [x] Build and smoke test pass: `cmake --build build --target draxul draxul-tests && py do.py smoke`.
- [ ] Run under TSan: `cmake --preset mac-tsan && cmake --build build --target draxul-tests && ctest --test-dir build -R draxul-tests`. No new data-race findings.
