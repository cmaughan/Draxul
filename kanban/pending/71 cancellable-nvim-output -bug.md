# Make editor writes bounded and cancellable

**Summary:** Make large editor input cancellable so an editor that stops reading cannot freeze the interface.

**Priority:** P1  
**Source:** `libs/draxul-nvim/src/rpc.cpp`

**Evidence and trigger:** B14; synchronous message writes block the interface when a nonreading editor exhausts pipe capacity.

- [x] **Investigate:** Trace request, notification, reply, and shutdown writes on both platforms.
  - `request()`, `notify()` (main thread: keys, paste, resize, actions, final
    `:qa!`), and `reply_to_request()` (reader thread) all wrote the full message
    synchronously under one mutex through blocking pipes (`write()` on POSIX,
    synchronous `WriteFile` on an anonymous pipe on Windows). A paste beyond pipe
    capacity to a non-reading child blocked the main thread indefinitely; the
    reader thread could also block replying, so it stopped draining Neovim.
    `NvimHost::shutdown()` sent `:qa!` through the same path before any teardown.
- [x] **Fix:** Introduce bounded outbound ownership and cancellable writes; cancel before shutdown waits.
  - `NvimRpc` owns a writer thread and one FIFO of encoded messages bounded by
    `kMaxOutboundBytes` (256 MiB); callers only encode and enqueue. A message that
    would exceed the bound is rejected whole, so framing and order of accepted
    messages are preserved. A writer failure fails the transport and wakes the
    owner once.
  - `NvimProcess::cancel_writes()` releases blocked writers: POSIX uses a
    non-blocking stdin pipe polled together with a cancel self-pipe; Windows
    registers writer threads and repeats `CancelSynchronousIo` until they leave.
    It waits a bounded 500 ms for writers to exit.
  - `NvimRpc::close()` stops intake, gives accepted output a 50 ms flush (so the
    final `:qa!` reaches a healthy editor), then cancels. `NvimProcess::shutdown()`
    also cancels before closing descriptors, so a blocked writer never sees its
    descriptor closed or reused underneath it.
- [x] **Acceptance:** Oversized input to a nonreading child leaves the interface responsive, preserves message ordering, and permits bounded shutdown.
  - `tests/rpc_integration_tests.cpp`: "nvim rpc output to a non-reading child
    keeps callers responsive and preserves order" (2 MiB payload plus 99 messages
    enqueue in under 1 s against a stalled child; after release all 100 echo back
    in order with the payload intact) and "nvim rpc shutdown is bounded while
    output is blocked on a non-reading child".
  - `tests/nvim_process_tests.cpp`: "nvim process cancel_writes releases a write
    blocked on a non-reading child" (platform transport level, both platforms).
- [x] **Validation (macOS):** Core aggregate tests and same-cache smoke.
  - Only known load-sensitive failures, both reproduced or passing independently
    of this change: `app dispatch: shared Neovim split retains actions until its
    host exists` also fails 3/4 runs with the original transport sources;
    `hidden remote terminal host suspends presentation...` passes 3/3 in
    isolation. `draxul-do-py-tests` fails because the worktree has no product
    submodules.
- [ ] **Validation (Windows):** Run `draxul-test-nvim-transport` on Windows to
  exercise the `CancelSynchronousIo` path (new process-level cancel test and both
  stalled-child RPC tests), plus the Windows core aggregate and smoke. The Windows
  code was written by careful reading only; it was not compiled here.
