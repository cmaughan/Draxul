# Handle closed editor pipes safely

**Summary:** Handle a closed editor connection safely so an input operation cannot terminate the application.

**Priority:** 68  
**Severity:** CRITICAL  
**Source:** `libs/draxul-nvim/src/nvim_process.cpp`

**Evidence and trigger:** B03; line 566 writes without parent-side signal suppression. An editor closing its input before a notification can terminate the macOS client.

- [x] **Investigate:** Trace parent signal handling and child inheritance, including the test runner’s existing suppression.
- [x] **Fix:** Suppress the signal for parent writes while preserving default child handling; coordinate cancellable writes with B14.
- [x] **Acceptance:** An isolated client with default initial signal handling survives a closed-pipe write and reports failure.
- [ ] **Validation:** Run core aggregate tests and same-cache smoke; verify the macOS production path.

## Implementation notes (2026-10-02)

- POSIX Neovim writes now block SIGPIPE only on their calling thread. On EPIPE,
  they consume a newly generated pending SIGPIPE before restoring the caller's
  mask. Existing pending signals and process-wide handlers are preserved. Failed
  signal-mask setup returns failure without attempting an unsafe write.
- Uses `sigwait` after confirming a pending signal, which is available on macOS;
  does not depend on Linux-only `sigtimedwait` or introduce a global ignore policy.
- Child startup restores SIGPIPE's default handler and explicitly unblocks it,
  including when the launching parent thread had it blocked.
- Added isolated POSIX regressions that bypass the test runner's SIG_IGN: repeat
  closed-pipe writes with default policy, preserve an already blocked/pending
  signal, and check an exec'd child's default unblocked signal policy.
- [B14/write cancellation](71%20cancellable-nvim-output%20-bug.md) remains its own tracked scope; this fix wraps the existing
  partial-write/EINTR loop without changing descriptor or cancellation ownership.
- Root owns aggregate/smoke validation. Native macOS execution is still required
  before completing this card; Windows compilation cannot exercise the POSIX branch.

## POSIX validation evidence (2026-10-02)

- WSL Ubuntu 22.04: a standalone `c++ -std=c++20 -pthread -O0 -g -Wall -Wextra`
  harness compiled the unchanged production `nvim_process.cpp`, `process_util.cpp`,
  `log.cpp`, and `perf_timing.cpp`, without another CMake cache or fetched dependencies.
- All three isolated process scenarios passed (exit status 0): twenty closed-pipe
  writes with default SIGPIPE policy; twenty closed-pipe writes preserving an
  already blocked/pending SIGPIPE; and an exec'd editor restoring default/unblocked
  SIGPIPE while its parent retains ignored/blocked policy.
- Harness and executable: `/tmp/draxul-sigpipe-validation-20261002/` in that WSL
  distribution. This exercises the real Linux production POSIX path; native macOS
  runtime validation remains outstanding.

- Final Windows core/product aggregate exercised both Neovim transport shards
  successfully (79/84 CTest entries overall in 372.89s; failures recorded separately
  in the atlas and checkpoint cards). Same-cache Debug smoke passed with an isolated
  APPDATA profile, and final Release Neovim startup exited 0. Windows evidence does
  not substitute for the explicit macOS production-path gate, which remains open.
- Validation used the existing Ninja Debug cache. The final incremental aggregate
  build took 22.75s; a preceding full validation took 309.66s for 77 CTest entries and
  26.13s for five passing core snapshots. The aggregate was repeated after correcting
  two test failures in other slices; snapshots were not repeated. No remote CI was run.
