# Move synchronous server IPC off the UI thread

**Type:** bug
**Priority:** P1 / sequence 17
**Raised by:** Claude (server/client runtime review, 2026-07-30)
**Evidence:** [plans/reviews/2026-07-30-server-client-runtime-review.md](../../plans/reviews/2026-07-30-server-client-runtime-review.md#blocking-ipc-on-threads-that-must-not-block)

## Goal

A slow or wedged server must degrade the UI, not freeze it. Today the render thread makes
blocking IPC calls every frame, so a stalled server stops SDL event handling and rendering
entirely — repeatedly, for as long as the stall lasts.

## Observed behaviour

The terminal data path got this right: `RemoteTerminalHost` polls on a worker and the main
thread only consumes published state. Nothing else did.

Blocking `ControlClient::request` calls on the UI thread:

- `poll_remote_topology()` and `poll_remote_agents()` from `pump_once`, every frame, gated to
  10 Hz each (`app/app.cpp:2266-2267`, `:3047-3106`).
- `flush_pending_remote_split_ratio()` — one `SetSplitRatio` round trip **per frame** while a
  divider is dragged, toasting per frame on failure (`app/app.cpp:3985-4009`).
- `execute_remote_topology_command` — up to two execute plus refresh round trips per
  split/close/resize (`app/app.cpp:3681-3699`).
- `ServerClient::status` inline from `on_server_status` and the stop-server prompt
  (`app/app.cpp:999`, `:1621`).
- `RemoteTerminalClient::attach` for each newly projected pane, inside
  `reconcile_projected_layout`, serially.

The cost is worse than the documented five-second bound. `ControlClient::request` re-reads the
metadata file from disk on every call and opens a fresh pipe or socket
(`libs/draxul-control/src/control_plane.cpp:1023-1106`), and `kIoTimeout` is applied **per I/O
operation** — `WaitNamedPipeW` at 5 s, then 5 s per overlapped read/write chunk; on POSIX
`SO_RCVTIMEO`/`SO_SNDTIMEO` per `recv`/`send`, each partial completion resetting the clock. One
stalled request can hold the UI thread 10-15 s, and the 100 ms cadence means it repeats.

Startup blocks too: `ServerClient::ensure` spins up to its 10 s timeout before any window
exists (`app/main.cpp:581`, `libs/draxul-client/src/server_client.cpp:305-364`), so a slow
first server start shows nothing at all, then either a window or an error box. The pre-server
default showed a terminal in well under a second.

The plan's known-boundaries section acknowledges bounded synchronous IPC for "topology
commands and projection polling", but understates both the reach (drags, status, attach) and
the duration (per-op, repeated).

## Implementation

- [x] Move topology and agent polling onto the worker-thread + published-state pattern
      `RemoteTerminalHost::Impl` already implements. The main thread should only consume a
      snapshot the worker prepared.
- [x] Make divider drags optimistic-local with a single trailing authoritative commit, instead
      of a round trip per frame.
- [x] Make pane attach asynchronous, with a placeholder pane until the first snapshot arrives,
      so projecting N panes does not serialize N synchronous attaches on the render thread.
- [x] Make `ServerClient::status` calls from GUI actions asynchronous, or give main-thread
      requests a short deadline (~100 ms) distinct from the transport's 5 s.
- [x] Give `ControlClient::request` an absolute per-request deadline in addition to the
      per-operation timeouts, so a trickling peer cannot extend a call indefinitely.
- [x] Cache the endpoint metadata read rather than hitting the filesystem on every request.
- [x] Show the window before `ensure()` completes, with a "connecting to server" surface, or
      drop the pre-window timeout to ~2 s and handle the remainder in-window.
- [x] Latch apply-failure and drag-failure toasts the way `topology_poll_error_announced_`
      already latches poll failures, so a persistent failure does not strobe the toast stack.

## Unit tests

- [ ] With a deliberately stalled server, frame pumping continues and the window keeps
      rendering; no main-thread call exceeds the short deadline.
- [x] Topology and agent state still converge through the worker path (port the existing
      convergence assertions).
- [ ] A drag produces one committed `SetSplitRatio`, not one per frame.
- [ ] Repeated apply failures produce one toast, not one per poll.

## Acceptance criteria

- [ ] A wedged-but-alive server leaves the UI responsive: input, rendering, and pane switching
      all continue.
- [ ] Cold start shows a window within ~1 s even when the server takes seconds to come up.
- [x] No `ControlClient::request` call remains on the render thread without a short deadline.
- [ ] Full build, `ctest`, and smoke pass on both platforms.

## Dependencies and ownership

Independent of the P0 cards but should follow them — this is what makes every other failure
stop *feeling* like a hang, so it should land before the recovery work in
`kanban/done/18 server-discovery-recovery-wedges -bug.md` and
`kanban/done/19 client-recovery-state-machine -refactor.md` are judged. Overlaps `kanban/done/27
topology-projection-extraction -refactor.md`: if both are scheduled, extract the projection
first and give it the worker thread as part of the move.

Windows validation completed with the Release build, all core/app CTest shards, focused
deadline/cache/worker tests, and the repository smoke test. The remaining unchecked items
are manual stress or second-platform coverage.
