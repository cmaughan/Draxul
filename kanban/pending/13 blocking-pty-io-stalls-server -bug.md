# Keep blocking PTY I/O off the server state thread

**Type:** bug
**Priority:** P0 / sequence 13
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

- [ ] Give each terminal a bounded input queue drained with non-blocking or timed writes;
      return an explicit backpressure error to the controller when it is full rather than
      blocking. Keep the 64 KiB per-request cap as an admission check, not as the write size.
- [ ] Cap queued reader bytes per process. On overflow, pause reads or drop with an overflow
      flag that forces a core resync, so a detached chatty shell cannot grow without bound.
- [ ] Move process teardown waits off the state thread (reap asynchronously), so destroying a
      pane with a live process does not stall other Sessions.
- [ ] Add a loop-latency metric and a warning threshold, so the "no service call may block"
      invariant is observable rather than implicit. Surface it through the existing sanitized
      metrics rather than a new endpoint.

## Unit tests

- [ ] A fake runtime whose `send_input` blocks does not prevent other terminals from pumping
      or other clients' requests from being answered.
- [ ] Input into a non-draining PTY returns a backpressure error within a bounded time instead
      of hanging the caller.
- [ ] Reader overflow sets the resync flag and bounds memory; the client recovers via snapshot.
- [ ] Destroying a terminal with a live process does not stall the loop past the latency
      threshold.

## Acceptance criteria

- [ ] Ctrl-S in one server-owned shell leaves every other shell and client responsive.
- [ ] A detached shell producing continuous output has bounded server memory.
- [ ] Loop latency is measurable and logged when it exceeds the threshold.
- [ ] Full build, `ctest`, and smoke pass on both platforms.

## Dependencies and ownership

Touches `draxul-terminal-process` and `draxul-server`. Shares the kernel loop with
`kanban/pending/12 server-crash-on-invalid-utf8 -bug.md`; one owner should hold both. The
backpressure error introduced here is the signal
`kanban/pending/15 oversized-paste-kills-remote-pane -bug.md` needs the client to handle
gracefully.
