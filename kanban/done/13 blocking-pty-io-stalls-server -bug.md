# Keep blocking PTY I/O off the server state thread

**Type:** bug
**Priority:** P0 / sequence 13
**Status:** Completed 2026-07-30
**Raised by:** Claude (server/client runtime review, 2026-07-30)
**Evidence:** [plans/reviews/2026-07-30-server-client-runtime-review.md](../../plans/reviews/2026-07-30-server-client-runtime-review.md#2-blocking-pty-io-can-wedge-the-singleton-kernel-thread)

## Goal

No single terminal may stall the shared server. The plan's own backpressure rule — "a slow
client is never allowed to block the server state loop or a PTY reader" — is currently
violated by the input path, and the reader queues that were meant to be bounded are not.

## Observed behaviour

The kernel runs everything on one 25 ms loop thread: pump every terminal, handle every client
request, refresh agents, checkpoint (`libs/draxul-server/src/server_kernel.cpp:2557-2696`).
That makes service code lock-free by construction, which is right — but it creates an implicit
"no service call may block" invariant that is already broken:

- **Input writes block unboundedly.** `terminal.input` accepts up to 64 KiB per request
  (`libs/draxul-server/src/remote_terminal_service.cpp:352-358`) and runs on the kernel thread
  via `ServerTerminalRuntime::send_input` → `process_.write()`. That is `WriteFile` with no
  `OVERLAPPED` or timeout (`libs/draxul-terminal-process/src/conpty_process.cpp:817-828`) and a
  `::write` retry loop (`libs/draxul-terminal-process/src/unix_pty_process.cpp:613-627`). A
  child that stops reading — Ctrl-S, a stopped foreground app, a paste flood into a busy TUI —
  never returns. PTY draining stops, checkpoints stop, every client of every Session gets
  `main_thread_timeout`. `agent.send_text` shares the path.
- **Reader queues are unbounded.** Both readers append with no cap
  (`conpty_process.cpp:850-853`, `unix_pty_process.cpp:680-683`) and the only consumer is the
  25 ms pump. Whenever the loop stalls, a chatty shell (`yes`, a build) grows server memory
  without limit.
- **Teardown blocks too.** `ConPtyProcess::shutdown` waits up to two seconds per destroyed live
  terminal (`conpty_process.cpp:658-659`) on the same thread.

## Implementation

- [x] Give each terminal a bounded input queue drained with non-blocking or timed writes;
      return an explicit backpressure error to the controller when it is full rather than
      blocking. Keep the 64 KiB per-request cap as an admission check, not as the write size.
- [x] Cap queued reader bytes per process. On overflow, pause reads or drop with an overflow
      flag that forces a core resync, so a detached chatty shell cannot grow without bound.
- [x] Move process teardown waits off the state thread (reap asynchronously), so destroying a
      pane with a live process does not stall other Sessions.
- [x] Add a loop-latency metric and a warning threshold, so the "no service call may block"
      invariant is observable rather than implicit. Surface it through the existing sanitized
      metrics rather than a new endpoint.

## Unit tests

- [x] A real non-reading PTY saturates its bounded writer queue without preventing a second
      terminal's input or metrics request from being answered.
- [x] Input into a non-draining PTY returns a backpressure error within 100 ms instead of
      hanging the caller.
- [x] Reader-queue backpressure pauses the reader at its byte cap and resumes after draining,
      preserving the complete byte stream without requiring a lossy-overflow resync.
- [x] Destroying a terminal with a live process returns within 100 ms and the detached reaper
      terminates the process.

## Acceptance criteria

- [x] The non-reading-process equivalent of Ctrl-S leaves another server terminal responsive.
- [x] A detached shell producing continuous output is bounded by the per-process output queue.
- [x] Loop latency is measurable and logged when it exceeds the threshold.
- [x] Windows Release/Ninja core and app test targets build; focused remote-terminal,
      PTY output-cap, teardown, and multi-terminal backpressure suites pass. The macOS
      PTY path remains covered by CI.

## Validation

- Windows Release/Ninja `draxul-test-core` and `draxul-test-app` build passed.
- `[server][remote-terminal][backpressure]`: 28 assertions in 3 cases.
- PTY output-cap preservation: 133 assertions; live-process teardown: 5 assertions.
- macOS/POSIX runtime validation remains a CI gate.

## Dependencies and ownership

Touches `draxul-terminal-process` and `draxul-server`. Shares the kernel loop with
`kanban/done/12 server-crash-on-invalid-utf8 -bug.md`; one owner held both. The
backpressure error introduced here is the signal
`kanban/done/15 oversized-paste-kills-remote-pane -bug.md` needs the client to handle
gracefully.
