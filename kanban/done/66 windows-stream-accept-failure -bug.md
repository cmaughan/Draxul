# Avoid waiting for nonexistent connection operations

**Summary:** Finish failed Windows connection attempts promptly so they cannot stop new connections or block shutdown.

**Priority:** P1 — Failed stream connections can block new clients and server shutdown.
**Source:** `libs/draxul-control/src/async_frame_stream_win32.cpp`

**Evidence and trigger:** B11; synchronous connection failure reaches an infinite wait on an unsignaled event without pending work.

- [x] **Investigate:** Distinguish synchronous success, synchronous failure, pending completion, and cancellation paths.
- [x] **Fix:** Cancel and drain only genuinely pending connection operations.
- [x] **Acceptance:** Immediate disconnect and injected synchronous errors return promptly; listener shutdown completes.
- [x] **Validation:** Run Windows control/server coverage, core aggregate tests, and same-cache smoke.

## Progress

- [x] Confirmed in current source: `AsyncFrameStreamListener::accept()` ran
      `CancelIoEx` + `WaitForSingleObject(event, INFINITE)` for every
      `!connected` outcome. A synchronous `ConnectNamedPipe` failure such as
      `ERROR_NO_DATA` (client connected and closed before the call) starts no
      operation, so the event never signals and the Session stream acceptor
      thread hung, blocking new connections and `SessionStreamService::stop()`.
- [x] `accept()` now tracks whether the connect is in flight. Only an
      `ERROR_IO_PENDING` connect that has not completed is cancelled and
      drained (`GetOverlappedResult(..., TRUE)`); synchronous success,
      `ERROR_PIPE_CONNECTED`, and synchronous failures return immediately with
      `io_error`, which the acceptor loop already treats as retryable.
- [x] Added a private Windows-only seam
      (`libs/draxul-control/src/async_frame_stream_test_hooks.h`) and the
      `Windows Session stream accept finishes synchronous connect failures
      promptly` case in `tests/control_transport_tests.cpp`. Its two sections
      connect-and-close a real client between `CreateNamedPipeW` and
      `ConnectNamedPipe`, and inject `ERROR_ACCESS_DENIED`; each then checks a
      prompt return, a following successful accept and frame exchange, and that
      `stop()` interrupts a pending accept.
- [x] Windows: build and run `draxul-test-client` (`[session-stream][windows]`)
      on Windows CI or a Windows host. Both synchronous-failure sections ran
      in the parent Windows aggregate below; isolated-profile same-cache smoke
      and aggregate accounting are recorded in the closure evidence.

### Windows execution evidence — 2026-10-08

- Parent run `build-ninja-debug/validation-logs/20261008-150658-521060/`:
  `draxul-test-client-shard-1` passed all 21 cases / 299 assertions in 3.24 s
  without skips (seed 2272956198). Shard 0 also passed 21 cases / 261 assertions
  in 1.26 s.
- Listed the existing executable with matching shard count/index/seed and
  `--list-tests --verbosity high`; the Windows synchronous-connect-failure case
  is in shard 1. Matching registry/pass counts and no skips establish execution
  of both sections: disconnect-before-connect and injected ERROR_ACCESS_DENIED,
  followed by successful frame exchange and interruption of pending accept.
- No extra test run or rebuild was performed by this worker. Default-profile
  smoke failed the existing server-startup deadline tracked in
  `kanban/pending/65 windows-validation-timing -test.md`; the subsequent
  isolated-profile same-cache smoke passed as recorded below.

### Closure evidence — 2026-10-08

- Parent confirmed `py do.py smoke debug --skip-build` passed after the full
  tests with APPDATA and LOCALAPPDATA set to
  `D:/dev/Draxul/build-windows-smoke-profile`. This reuses the tested Ninja Debug
  executable; default-profile startup remains tracked in
  `kanban/pending/65 windows-validation-timing -test.md`.
- Full behavioral aggregate: 73/79 CTest entries passed in 232.92 s, not a
  green aggregate. Failures outside this card were `draxul-test-server-shard-0`,
  `draxul-test-font-shard-0`, `draxul-test-core-shard-1`,
  `draxul-test-nvim-transport-shard-1`, `draxul-test-scope-integration`, and
  `draxul-test-personal-host-shard-0`. The Nvim shutdown failure and subsequent
  repair are recorded in `kanban/done/71 cancellable-nvim-output -bug.md`.
- Closed with the exact Windows accept case and both sections executed
  successfully, plus isolated same-cache startup.
  No extra focused runtime pass or build was performed here. Configure/compile
  costs belong to the parent's three-attempt validation record; isolated smoke
  duration was not supplied.
- Final Release startup: parent confirmed `py do.py smoke release --skip-build`
  passed exit 0 in 33.866647 s overall. Log:
  `build-ninja-debug/windows-gates/final-release-smoke-retry.log`. The first
  attempt failed during MSVC environment initialization under machine resource
  exhaustion after 68.059 s, before application startup; it is not hidden by
  the successful retry. Release includes the Nvim shutdown correction.

### macOS validation — 2026-10-08 (Debug)

- `python3 do.py test debug` built the shared test header and the POSIX side
  of `tests/control_transport_tests.cpp`; `draxul-test-client` passed. The
  aggregate's unrelated failures are listed in
  `kanban/done/62 topology-durable-limits -bug.md`.
- `python3 do.py smoke debug --skip-build` passed.
