# Make editor writes bounded and cancellable

**Summary:** Make large editor input cancellable so an editor that stops reading cannot freeze the interface.

**Priority:** P1 — A stalled editor can freeze input and delay application shutdown.
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
- [x] **Validation (Windows):** Run `draxul-test-nvim-transport` on Windows to
  exercise the `CancelSynchronousIo` path (new process-level cancel test and both
  stalled-child RPC tests), plus the Windows core aggregate and smoke. The Windows
  run exposed a blocked-read teardown defect; the correction below passed the
  parent's rebuilt transport shards and post-fix same-cache smoke.

## Windows aggregate diagnosis — 2026-10-08

- Parent validation logs: `build-ninja-debug/validation-logs/20261008-150658-521060/`.
  Transport shard 0 passed 28 cases / 185 assertions in 34.25 s, including the
  process-level `cancel_writes` case. Shard 1 executed 28 cases with no skips;
  27 passed, including stalled-child ordering/responsiveness, but the bounded
  shutdown case failed: 29.8494532 s against the unchanged 4 s bound (31.54 s
  shard total). This is relevant failure evidence, not an unrelated flake.
- Cause: Windows `shutdown()` closed the synchronous stdout pipe while the RPC
  reader was blocked in `ReadFile`, before launching its process reaper. That
  close can wait for the pending read, preventing the reaper's 2 s termination
  deadline from starting. The observed delay matches the fake child's 30 s
  stall deadline, minus setup and the initial output-cancellation interval.
- Correction: immediately unpublish stdout, then transfer its handle with the
  process handle to the existing detached reaper. The reaper waits/escalates
  first, then closes stdout after child exit releases the reader. It captures
  handles only, not `Impl`, preserving the UI owner's independent lifetime.
  POSIX shutdown already schedules its reaper after nonblocking descriptor
  close and is unchanged; outbound cancellation and ordering are unchanged.
- Extended the existing real-process regression to require `process.shutdown()`
  itself to return within 500 ms, separately from the original complete
  process/RPC shutdown bound of 4 s. No timeout was increased and no retry-to-green
  was run. Acceptance is reopened until Windows validation proves the repair.
- Parent owns builds and reruns. This worker made no build/test invocation for
  the correction; earlier executable invocations only listed registered cases.

## Windows correction validation — 2026-10-08

- Parent rebuilt the focused target: 4 build steps, with automatic reconfigure
  triggered by a newly registered Rezonality test. Separate configure/compile
  durations were not supplied. No source changes followed the correction.
- Focused filter `*non-reading child*` passed all three cases on each of seeds
  7108, 7109 and 7110: 22 assertions / 3 cases per run, 9.00 s total test time.
  This covers process-level write cancellation, RPC ordering/responsiveness,
  and bounded stalled-child shutdown, including the new 500 ms process-return
  assertion and the unchanged 4 s complete-shutdown bound.
- Diagnostic chain: the original Windows aggregate passed write cancellation
  and ordering but failed complete shutdown at 29.8494532 s. Inspection found
  synchronous stdout handle closure ahead of reaper launch; its delay matched
  the fake child's 30 s stall. Moving only that handle's cleanup behind the
  reaper's exit/termination step, without changing cancellation or relaxing
  timeouts, now passes both the separate caller-return and total-shutdown
  assertions in all three seeded runs. This is post-fix evidence, not reruns
  of unchanged failing code, and does not claim native-stack tracing.
- Final aggregate and startup evidence follow. The earlier isolated Debug smoke
  preceded this correction and is not its final startup evidence. No additional
  code change followed the focused passes.

## Final Windows aggregate evidence — 2026-10-08

- Verified `build-ninja-debug/windows-gates/final-core-rezonality.log` and
  `build-ninja-debug/windows-gates/final-core-rezonality-ctest.log`: transport
  shard 0 passed 28 cases / 185 assertions in 32.42 s (seed 2297481033);
  shard 1 passed 28 cases / 165 assertions in 5.38 s (seed 1571474538).
  All 56 transport cases passed without skips, including the three previously
  mapped blocked-child cases and the strengthened process-return assertion.
- Parent's core + Rezonality aggregate built 19 steps in the existing Debug
  cache and passed 64/70 CTest entries. CTest wall time was 306.89 s; the wrapper
  reported 308.27 s. This is not a green aggregate: remaining failures concern
  server/personal state, font fallback, scope selection, remote initial state,
  personal-host path decoding and native Vulkan validation, not this transport
  correction. Detailed failures remain in the logs rather than being hidden
  by the successful transport shards.
- Acceptance is complete. Post-fix same-cache startup evidence follows.

## Closure — 2026-10-08

- Parent confirmed paired `py do.py smoke debug --skip-build` passed exit 0
  after the final aggregate. Log:
  `build-ninja-debug/windows-gates/final-debug-smoke.log`. Measured overall time
  was 35.273 s, including MSVC environment setup; it is not application-only
  startup time. This is post-fix evidence from the same Debug cache.
- Closed on passing exact cancellation/ordering/shutdown cases, both complete
  transport shards and paired same-cache smoke, with the aggregate's 64/70
  result and outside-transport failures explicitly retained above.
- Final Release startup: parent confirmed `py do.py smoke release --skip-build`
  passed exit 0 in 33.866647 s overall. Log:
  `build-ninja-debug/windows-gates/final-release-smoke-retry.log`. The first
  attempt failed during MSVC environment initialization under machine resource
  exhaustion after 68.059 s, before application startup; it is not hidden by
  the successful retry. Release includes the Nvim shutdown correction.
- No production edits followed the validated correction, and this worker ran
  no competing builds/tests. The parent owns the extracted-package check.
