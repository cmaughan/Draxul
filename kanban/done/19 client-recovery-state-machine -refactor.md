# Unify remote client recovery into one state machine

**Type:** refactor
**Priority:** P1 / sequence 19
**Raised by:** Claude (server/client runtime review, 2026-07-30)
**Evidence:** [plans/reviews/2026-07-30-server-client-runtime-review.md](../../plans/reviews/2026-07-30-server-client-runtime-review.md#recovery-mechanisms-that-depend-on-what-they-recover)

## Goal

There are four independent, inconsistent recovery policies across the client today. Replace
them with one owner of backoff, epoch refresh, and reattach, so failures behave the same way
everywhere and typed input is never silently lost.

Doing these separately produces three partial fixes; the point of this card is that they are
one problem.

## Observed behaviour

**Four policies.**

1. The host worker's five-second grace (`libs/draxul-host/src/remote_terminal_host.cpp:29`)
   is wall-clock, but each failing request against a hung server itself consumes ~5 s, so the
   second failure lands past the grace. It amounts to roughly one retry, after which the pane
   dies permanently (`:342-348`) — see `kanban/pending/15` for the no-recovery consequence.
2. The topology client retries forever with an announce-once toast and no backoff
   (`app/app.cpp:3058-3066`).
3. `refresh_scrollback_after_output` failure is instantly fatal with no classification at all
   (`remote_terminal_host.cpp:393-401`), so the same blip that is tolerated at live view kills
   the pane when the user happens to be scrolled back.
4. The server epoch is pinned once at process start — `host_factory` captures
   `remote_options` by value (`app/main.cpp:698-717`) and `attach` hard-fails on mismatch
   (`libs/draxul-client/src/remote_terminal_client.cpp:256-263`). After a real server restart,
   reattach always fails *and* every newly projected pane is built with the stale epoch, so
   even a successful topology re-projection cannot produce a working host.

**No at-most-once contract.** `dispatch()` abandons the wait on timeout but leaves the
`Pending` in the queue, and `process_pending` later runs the handler unconditionally
(`libs/draxul-control/src/control_plane.cpp:659-697`). So a command can be applied server-side
while the client believes it failed. Meanwhile the host worker *drops* the unexecuted
remainder of its command batch on transient failure — `break` at
`remote_terminal_host.cpp:339` abandons up to `kRemoteCommandsPerPoll` (8) queued commands with
no requeue and no feedback — so keystrokes vanish during a brief hiccup.

**No backoff or jitter anywhere.** Topology and agent polls are fixed at 100 ms, the host
worker at 25 ms even while failing, so every UI hammers the four listener instances in
lockstep.

## Implementation

- [x] Introduce one client connection state machine owning: connected/degraded/reconnecting
      states, exponential backoff with jitter, epoch refresh, and reattach by `terminal_id`.
- [x] Source the expected epoch from mutable, App-owned connection state refreshed via
      `ServerClient::probe` when `stale_epoch` is observed, instead of a value captured at
      process start.
- [x] Count retry *attempts* rather than wall time, so the grace is not consumed by the
      timeouts it is meant to survive.
- [x] Route `refresh_scrollback_after_output` failures through the same classification as
      `poll`.
- [x] Attempt one `recover_attachment()` on `invalid_event`/`stale_sequence` before declaring
      fatal — `attach()` fully resynchronizes, so a single bad event should not be terminal.
- [x] Requeue unexecuted commands on transient failure instead of dropping them, and retry
      after backoff.
- [x] Add cancellation to the transport: carry a flag in `Pending`, set it on timeout, and
      have `process_pending` skip cancelled entries. Make mutating methods idempotent via the
      request id — `topology.command` already has `command_key` (`client_id` + `command_id`,
      `libs/draxul-server/src/topology_service.cpp:77-80`); terminal and agent methods need the
      same treatment.
- [x] Map `unknown_method` to a gentle failure rather than pane death, so a capability the
      server lacks does not kill a pane on a mere scroll.

## Unit tests

- [x] A server restart with a new epoch is recovered in place: existing panes reattach and
      newly projected panes work, without reopening the UI.
- [x] A >10 s server stall followed by recovery leaves every pane alive.
- [x] Commands queued during a transient failure are delivered after recovery, in order.
- [x] A request that times out at the transport is not executed server-side.
- [x] A single malformed event triggers reattach, not a dead pane.
- [x] Backoff grows and includes jitter; failing clients do not poll in lockstep.

## Acceptance criteria

- [x] One documented recovery policy applies to terminals, topology, and agents.
- [x] No user keystroke is dropped without either delivery or a visible message.
- [x] Server restart is recoverable without reopening the UI, closing the plan's known
      boundary on epoch migration.
- [ ] Full build, `ctest`, and smoke pass on both platforms.

## Validation

- [x] Windows Release/Ninja production build and all six core/app CTest shards pass.
- [x] Debug/Ninja smoke passes.
- [x] Focused coverage passes for an actual 10.25-second server outage and new-epoch
      restart, existing/new pane attachment, ordered input retry, transport cancellation,
      terminal and agent mutation deduplication, malformed-event reattach, and bounded
      jittered backoff.
- [ ] macOS/POSIX build and runtime validation.

## Dependencies and ownership

Subsumes the permanent-ghost half of `kanban/pending/15
oversized-paste-kills-remote-pane -bug.md`; if both are scheduled together, one owner should
hold `remote_terminal_host.cpp`. Best sequenced after `kanban/pending/17
sync-ipc-on-ui-thread -bug.md`, since the worker-thread move changes where these policies run.
Closes the first entry in the plan's "Known boundaries retained for later work".
