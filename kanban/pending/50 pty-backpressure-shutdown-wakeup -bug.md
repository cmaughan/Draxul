# Synchronize PTY stop predicates with output waits

**Severity:** HIGH  
**Reported by:** Claude Fable 5.1

`libs/draxul-terminal-process/src/unix_pty_process.cpp:298` and `conpty_process.cpp:854` change the reader stop predicate outside `output_mutex_`. Shutdown racing the backpressure wait can lose notification and block a join indefinitely. Unix `request_close()` also omits waking that condition variable.

**Investigation**

- [x] Force shutdown between predicate evaluation and blocking with a full output queue.

**Fix strategy**

- [x] Change the relevant stop predicate under the waiter’s mutex.
- [x] Notify the backpressure condition on every stop/close path.

**Acceptance criteria**

- [ ] Saturated readers exit within a bounded deadline on both platforms (macOS focused coverage passes; Windows pending).
- [x] Normal backpressure still preserves output ordering and contents.
