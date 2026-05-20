# WI 90 — pty-sigpipe-child-inherit

**Type:** bug  
**Priority:** 1 (incorrect shell pipeline behaviour)  
**Source:** review-bugs-consensus.md §H4 [GPT]  
**Produced by:** claude-sonnet-4-6

---

## Problem

`libs/draxul-host/src/unix_pty_process.cpp:39` calls `signal(SIGPIPE, SIG_IGN)` before `fork()` to suppress fatal `SIGPIPE` in the parent process (safe for a GUI app). However, the child code block (lines 72–106) does not restore `SIGPIPE` to `SIG_DFL` before `execvp()`. Every shell and its subprocesses inherit the ignored disposition, breaking standard Unix pipeline termination.

Concrete repro: In a terminal pane, run `yes | head -n1`. `yes` should die from `SIGPIPE` when `head` exits; instead it keeps running or errors with `EPIPE`.

---

## Investigation

- [x] Read `libs/draxul-host/src/unix_pty_process.cpp:37–107` — confirm the parent sets `SIG_IGN`, the child does not restore it, and `execvp` inherits the disposition.
- [x] Check `libs/draxul-host/src/nvim_process.cpp` (POSIX path) and `shell_host_unix.cpp` — confirm whether they spawn processes and whether they have the same issue.

---

## Fix Strategy

- [x] In the child code block, before `execvp()`, add:
  ```cpp
  signal(SIGPIPE, SIG_DFL);
  ```
- [x] Apply the same fix to any other child code blocks in the codebase that spawn shells or subprocesses.
- [x] Build: `cmake --build build --target draxul draxul-tests`
- [ ] Run smoke test: `py do.py smoke`

---

## Acceptance Criteria

- [ ] `yes | head -n1` in a terminal pane terminates cleanly without hanging.
- [ ] `SIGPIPE` disposition is `SIG_DFL` in all spawned child processes.
- [ ] Smoke test passes.
