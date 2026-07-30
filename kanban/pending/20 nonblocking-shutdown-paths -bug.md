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

- [ ] Check `stopping_` between commands in the host worker loop so a stop takes effect at the
      next command boundary rather than after the whole batch.
- [ ] Skip the worker-side `disconnect` when stopping, and issue `server.goodbye` best-effort
      with a short (~250 ms) deadline or from a detached thread.
- [ ] Time-box or detach the worker join so one unresponsive pane cannot hold exit.
- [ ] In `ControlServer::Impl::stop()`, set a stopping flag and drain/fail the queue **before**
      `request_stop()`/`join()`, and have `dispatch()` return immediately once that flag is set.
- [ ] Audit the remaining synchronous calls on teardown paths (`App::shutdown`,
      `request_close`, destructors) and give each an explicit bounded deadline.

## Unit tests

- [ ] Closing a pane whose server never responds completes within a bounded time.
- [ ] `App::shutdown` with N remote panes against a hung server completes within a bounded
      time that does not scale with N × timeout.
- [ ] `ControlServer::stop()` fails pending promises promptly rather than after the join.
- [ ] A stop requested mid-batch is honoured at the next command boundary.

## Acceptance criteria

- [ ] Quitting Draxul with several remote panes against a hung server takes seconds, not
      minutes.
- [ ] `ServerKernel::stop()` does not block on listener threads waiting for a dead main loop.
- [ ] No teardown path makes an unbounded synchronous request.
- [ ] Full build, `ctest`, and smoke pass on both platforms.

## Dependencies and ownership

Shares `remote_terminal_host.cpp` with `kanban/pending/15` and `kanban/pending/19`, and
`control_plane.cpp` with `kanban/pending/17 sync-ipc-on-ui-thread -bug.md` (the absolute
per-request deadline added there is the same mechanism this card needs on teardown). Schedule
after or alongside `17`. Related existing card:
`kanban/ice-box/69 concurrent-host-shutdown -test.md`.
