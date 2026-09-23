# Keep server and client shutdown non-blocking

**Type:** bug
**Priority:** P1 / sequence 20
**Raised by:** Claude (server/client runtime review, 2026-07-30)
**Evidence:** [plans/reviews/2026-07-30-server-client-runtime-review.md](../../plans/reviews/2026-07-30-server-client-runtime-review.md#blocking-ipc-on-threads-that-must-not-block)

## Goal

Honour CLAUDE.md's rule that shutdown paths stay non-blocking. Closing a window or a pane
against an unresponsive server can currently block for minutes.

## Observed behaviour

**Client side.** `App::shutdown()` runs `space_controller_.shutdown_all()` then a synchronous
`ServerClient::disconnect` (`app/app.cpp:5358-5376`). Each `RemoteTerminalHost::stop()` sets
`stopping_` and joins the worker (`libs/draxul-host/src/remote_terminal_host.cpp:146-153`) —
but the worker's command loop does not check `stopping_` between commands
(`:283-296`), so the join waits through up to `kRemoteCommandsPerPoll` (8) synchronous
requests, each up to the per-operation transport timeout. The worker then sends one *more*
synchronous `client_->disconnect(...)` before exiting (`:406-408`). `request_close()` and the
destructor both call this on the main thread. With several panes that is tens of seconds to
minutes of frozen UI on exit.

These goodbyes are redundant safety: the server already expires inactive clients after
`client_activity_timeout` (default 10 s).

**Server side.** `ControlServer::Impl::stop()` joins the listener threads *first* and only then
fails the pending promises (`libs/draxul-control/src/control_plane.cpp:629-657`). A listener
sitting in `dispatch()` waits the full `kIoTimeout` for a main loop that has already exited,
and a listener in `read_frame` adds another timeout — so the ordering is exactly backwards.
The same `ControlServer` is embedded in the UI's `App`, so this hits both processes.

## Implementation

- [x] Check `stopping_` between commands in the host worker loop so a stop takes effect at the
      next command boundary rather than after the whole batch.
- [x] Skip the worker-side `disconnect` when stopping, and issue `server.goodbye` best-effort
      with a short (~250 ms) deadline or from a detached thread.
- [x] Time-box or detach the worker join so one unresponsive pane cannot hold exit.
- [x] In `ControlServer::Impl::stop()`, set a stopping flag and drain/fail the queue **before**
      `request_stop()`/`join()`, and have `dispatch()` return immediately once that flag is set.
- [x] Audit the remaining synchronous calls on teardown paths (`App::shutdown`,
      `request_close`, destructors) and give each an explicit bounded deadline.

## Unit tests

- [x] Closing a pane whose server never responds completes within a bounded time.
- [x] The shared coordinator with N remote-pane registrations tears down within
      a bounded time that does not scale with N × timeout; `App::shutdown`
      stops that coordinator before pane destruction.
- [x] `ControlServer::stop()` fails pending promises promptly rather than after the join.
- [x] A stop requested mid-batch is honoured at the next command boundary.

## Acceptance criteria

- [x] Quitting Draxul with several remote panes against a hung server takes seconds, not
      minutes.
- [x] `ServerKernel::stop()` does not block on listener threads waiting for a dead main loop.
- [x] No teardown path makes an unbounded synchronous request.
- [x] macOS full build, `ctest`, and smoke pass.
- [x] Windows full build, `ctest`, and smoke pass.

## Dependencies and ownership

Shares `remote_terminal_host.cpp` with
`kanban/done/15 oversized-paste-kills-remote-pane -bug.md` and
`kanban/done/19 client-recovery-state-machine -refactor.md`, and
`control_plane.cpp` with `kanban/done/17 sync-ipc-on-ui-thread -bug.md` (the absolute
per-request deadline added there is the same mechanism this card needs on teardown). Schedule
after or alongside `17`. Related existing card:
`kanban/ice-box/69 dead-host-input-routing -test.md`.

Windows validation completed with focused host/control shutdown tests, all core/app CTest
shards, and the repository smoke test. The manual multi-pane exit stress gate is complete.

The 2026-09-23 audit extended the blocked-request coordinator regression to
twelve terminal registrations and verifies that releasing every registration
still takes less than one second. `App::shutdown` stops the one shared
coordinator before it destroys those pane registrations, so the shutdown bound
does not multiply by pane count. That regression exposed a legacy-fallback gap:
individual registration removal still waited up to 250 ms per blocked worker.
Unregistration now requests stop and transfers any outstanding join to a
self-retaining reaper immediately, so ordinary pane teardown is independent of
the transport timeout and pane count.

## 2026-09-23 pending-lane verification

- The focused twelve-registration blocked-request regression passed all 16
  assertions, including the shared sub-second release bound.
- An isolated live session created five remote panes, suspended the exact server
  process, and requested termination of the exact attached UI process. The UI
  exited successfully in about 7.4 seconds and logged the bounded client-release
  deadline instead of waiting for the suspended server. The server was then
  resumed and shut down cleanly. This completes the desktop observation.
