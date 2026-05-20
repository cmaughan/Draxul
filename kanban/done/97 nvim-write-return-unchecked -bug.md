# WI 97 — nvim-write-return-unchecked

**Type:** bug  
**Priority:** 2 (silent input discard after nvim pipe break)  
**Source:** review-bugs-consensus.md §M4 [Claude]  
**Produced by:** claude-sonnet-4-6

---

## Problem

`NvimProcess::write()` (`libs/draxul-nvim/src/nvim_process.cpp:376–386`) returns `false` when the pipe is broken (POSIX: `write()` returns -1; Windows: equivalent failure). Callers such as `NvimRpc::notify()` ignore the return value. After nvim exits unexpectedly, subsequent key events, resize notifications, and other RPC messages are silently discarded rather than triggering a clean shutdown sequence.

---

## Investigation

- [ ] Read `libs/draxul-nvim/src/rpc.cpp` — find `NvimRpc::notify()` and any other call sites of `process_->write()`.
- [ ] Read `libs/draxul-nvim/src/nvim_process.cpp:376–386` — confirm failure return path.
- [ ] Check whether the reader thread already sets `impl_->read_failed_ = true` on EOF/error and whether callers check that flag; understand what flag is the intended shutdown trigger.

---

## Fix Strategy

- [ ] In `NvimRpc::notify()` (and any other write call sites), check the return value:
  ```cpp
  if (!impl_->process_->write(buf.data(), buf.size()))
  {
      impl_->read_failed_ = true;
      if (on_notification_available)
          on_notification_available();
      return;
  }
  ```
- [ ] Ensure this triggers the same orderly shutdown path as an EOF on the read side.
- [ ] Build: `cmake --build build --target draxul draxul-tests`
- [ ] Run smoke test: `py do.py smoke`

---

## Acceptance Criteria

- [ ] A broken nvim pipe detected on write triggers the same shutdown sequence as EOF on read.
- [ ] No key events are silently discarded after nvim exits.
- [ ] Smoke test passes.

---

## Interdependencies

- **WI 88** (pty-pollhup-data-loss) — both address POSIX process/pipe error handling; can be done in the same pass.
