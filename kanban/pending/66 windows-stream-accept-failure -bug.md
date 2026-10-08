# Avoid waiting for nonexistent connection operations

**Summary:** Finish failed Windows connection attempts promptly so they cannot stop new connections or block shutdown.

**Priority:** P1  
**Source:** `libs/draxul-control/src/async_frame_stream_win32.cpp`

**Evidence and trigger:** B11; synchronous connection failure reaches an infinite wait on an unsignaled event without pending work.

- [x] **Investigate:** Distinguish synchronous success, synchronous failure, pending completion, and cancellation paths.
- [x] **Fix:** Cancel and drain only genuinely pending connection operations.
- [ ] **Acceptance:** Immediate disconnect and injected synchronous errors return promptly; listener shutdown completes.
- [ ] **Validation:** Run Windows control/server coverage, core aggregate tests, and same-cache smoke.

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
- [ ] Windows: build and run `draxul-test-client` (`[session-stream][windows]`)
      on Windows CI or a Windows host. The change was written on macOS, where
      this file and the new test case are not compiled, so neither compile
      correctness nor the runtime behavior has been checked on Windows yet.
      Tick Acceptance and Validation once that case, the Windows core
      aggregate, and a same-cache smoke pass there.

### macOS validation — 2026-10-08 (Debug)

- `python3 do.py test debug` built the shared test header and the POSIX side
  of `tests/control_transport_tests.cpp`; `draxul-test-client` passed. The
  aggregate's unrelated failures are listed in
  `kanban/done/62 topology-durable-limits -bug.md`.
- `python3 do.py smoke debug --skip-build` passed.
