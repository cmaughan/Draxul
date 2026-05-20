# UnixPtyProcess: use-after-free in detached reaper thread

**Severity:** HIGH  
**File:** `libs/draxul-host/src/unix_pty_process.cpp:139–227`  
**Source:** review-bugs-consensus BUG-03 (gemini)

## Bug Description

`shutdown()` moves the reader thread into a detached background thread that calls `reader.join()` after a bounded waitpid loop. The reader thread's lambda captures `this`. If the owning `HostManager` is destroyed before the detached thread finishes (up to ~600 ms during the kill/wait sequence), the `UnixPtyProcess` object is freed while the reader thread may still be accessing `this->reader_running_` and `this->on_output_available_`.

**Secondary data race:** `on_output_available_` is a non-atomic `std::function` set in `spawn()` (line 133) and called in `reader_main()` (line 354). If `spawn()` is called to reuse the object while the old reader thread is still alive inside the detached cleanup, there is a data race on this `std::function`.

**Trigger:** Rapid workspace close + create cycle, or a shell with a slow SIGTERM handler (e.g. `trap 'sleep 5' TERM`) combined with immediate re-use of the `UnixPtyProcess`.

## Investigation

- [ ] Trace the `HostManager` destruction path: confirm it does NOT wait for the detached reaper thread to finish
- [ ] Check if `UnixPtyProcess` is ever reused after `shutdown()` (i.e. `spawn()` called again on the same instance)
- [ ] Confirm the shutdown pipe + `reader_running_ = false` sequence makes the reader exit before `this` is invalidated in the common case
- [ ] Identify the exact code path where UAF is reachable (workspace close → `HostManager` destroy → `UnixPtyProcess` destroy before join)

## Fix Strategy

Option A (preferred if `UnixPtyProcess` is never reused):
- [ ] Join the reader thread synchronously in `shutdown()` before returning; the shutdown pipe guarantees near-instant exit so this adds negligible latency
- [ ] Leave the child waitpid in the detached thread (that path only captures primitives, not `this`)

Option B (if reuse of the same instance is required):
- [ ] Inherit `std::enable_shared_from_this<UnixPtyProcess>`
- [ ] Have the detached reaper lambda capture `shared_from_this()` to extend lifetime
- [ ] Guard writes to `on_output_available_` with a mutex also held in `reader_main()`

## Acceptance Criteria

- [ ] Destroying a `HostManager` immediately after `shutdown()` does not trigger ASAN/TSAN errors
- [ ] Run under TSan (`cmake --preset mac-tsan && ctest -R draxul-tests`): no data race on `on_output_available_`
- [ ] Rapid open/close of shell panes in smoke test does not crash or report sanitizer errors
- [ ] `shutdown()` returns within 10 ms in the normal case (reader exits via shutdown pipe)
