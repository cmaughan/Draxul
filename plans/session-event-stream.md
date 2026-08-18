# Multiplexed Session Event Stream Plan

**Tracker:** `kanban/pending/40 multiplexed-session-event-stream -feature.md`

**Sequencing:** diagnostics and UI coordination may start alongside
`kanban/pending/12 control-transport-boundary -refactor.md`; batched polling waits
for card 12, and the persistent endpoint also waits for
`kanban/pending/13 server-kernel-private-decomposition -refactor.md`.

## Goal

Replace per-pane timer polling with one multiplexed Session transport per attached UI.
The target architecture uses one persistent bidirectional UI-to-server connection for
terminal presentation, topology, agents, commands, responses, heartbeats, and recovery.
Delivery remains logically separated into channels so noisy terminal output cannot
starve commands or topology changes.

The migration must remain compatible with older clients and servers. A batched
Session poll is the first production milestone and remains the fallback transport
after persistent streaming is introduced.

## Problem

Every visible `RemoteTerminalHost` currently owns a worker that polls its terminal
every 25 ms. Ten visible panes can therefore issue roughly 400 short-lived control
requests per second. Topology and agent projection poll every 100 ms over the same
Unix-domain socket or named-pipe control endpoint.

The control server has four listener workers and a small connection backlog. Under a
multi-pane animated dashboard, terminal polls and CLI commands can occupy the endpoint
long enough for topology requests to fail. The client then reports the generic
`io_error: Control request failed` and the UI immediately displays a `Shared topology
unavailable` toast. A later poll normally succeeds; topology was not corrupted, but
the transport was saturated.

This scales by visible pane count when the desired unit of transport ownership is the
attached UI or Session.

## Decision

Use this migration sequence:

1. Add transport diagnostics and a repeatable multi-pane load test.
2. Centralize presentation delivery in one UI-level Session coordinator.
3. Add batched `session.poll` and remove per-pane transport polling.
4. Add a dedicated persistent Session event stream, retaining batched polling as a
   negotiated fallback.
5. Make the persistent stream bidirectional so commands and correlated responses can
   use the same physical connection.

Do not implement per-pane long polling on the current control endpoint. A long-lived
request would consume one of its four synchronous listener workers per pane or UI and
could reproduce endpoint starvation. Long polling is acceptable only as a fallback on
a dedicated asynchronous Session event endpoint; it is not the final architecture.

## Transport Model

The final transport is one physical connection per attached UI, multiplexing logical
channels:

- Session topology events and snapshots;
- agent state events and snapshots;
- terminal events for every subscribed visible pane;
- terminal input, resize, restart, controller, and scrollback commands;
- topology and agent commands;
- correlated command responses;
- subscription and presentation-visibility changes;
- heartbeats, server epoch changes, and recovery control messages.

One connection does not imply one unstructured FIFO. Every envelope identifies its
logical channel, correlation identity, and ordering cursor. Scheduling must prioritize
control and topology traffic over bulk terminal output and rotate fairly between busy
terminals.

Illustrative envelope:

```json
{
  "kind": "events",
  "request_id": 0,
  "channel": "terminal",
  "terminal_id": "terminal-8",
  "generation": 3,
  "after_sequence": 812,
  "events": []
}
```

The exact wire encoding may remain framed JSON initially. The design must preserve a
future binary encoding without changing channel semantics.

## Protocol Requirements

### Handshake and capability negotiation

- Add capabilities such as `session-poll-v1`, `session-stream-v1`, and later
  `session-stream-commands-v1`.
- Authenticate with the existing connection token and bind the stream to a client ID,
  Session ID, and server epoch.
- Negotiate frame and queue budgets.
- Fall back in order: bidirectional stream, event stream plus control commands,
  batched Session polling, then the legacy per-channel clients for older servers.

### Subscriptions and cursors

- The UI coordinator owns the set of visible terminal subscriptions.
- Each terminal cursor contains terminal ID, generation, and last applied sequence.
- Topology and agents retain their revision cursors.
- Subscribe, suspend, resume, and unsubscribe operations are idempotent.
- Hidden panes keep their existing presentation-suspension semantics: server runtimes
  continue headlessly without cell delivery to that UI.

### Ordering and recovery

- Preserve ordering within each logical channel; do not impose global ordering between
  unrelated terminals.
- Commands carry request IDs so reconnects can safely retry idempotent mutations using
  existing mutation caches or their Session-level equivalent.
- A cursor gap, generation change, queue overflow, or server epoch change triggers an
  authoritative channel snapshot rather than replaying an unbounded history.
- Reconnect sends the latest client cursor set. The server resumes where possible and
  snapshots only the channels that cannot resume.
- The client continues displaying the last coherent topology and terminal projection
  during a transient disconnect.

### Fairness and backpressure

- Bound queued bytes globally per UI and individually per channel.
- Reserve capacity for topology, heartbeat, command responses, and controller state.
- Batch adjacent terminal updates and coalesce replaceable state such as resize.
- Rotate the first terminal considered in each batch so a constantly updating pane
  cannot monopolize the payload budget.
- When a terminal exceeds its budget, mark only that terminal for snapshot resync.
- Disconnect or degrade a persistently slow UI without blocking the server state loop
  or other clients.

### Threading

- Socket reads and writes remain off the UI and server state threads.
- The server state thread publishes immutable or encoded event batches to a bounded
  per-connection writer queue; it never waits for network I/O.
- The UI Session coordinator decodes and routes updates to existing per-terminal
  projections, then coalesces them into one window wake.
- Grid and renderer mutation remains on the UI thread.
- Connection shutdown and reconnection remain bounded and cancellable.

## Implementation Phases

### Phase 0: Diagnostics and baseline

- Preserve the failing operation, transport stage, and platform error (`errno` or
  Windows error) instead of collapsing all failures into `io_error`.
- Add metrics for connection attempts, accepted connections, listener occupancy,
  request counts by method, queue time, dispatch time, response time, and failures by
  transport stage.
- Add deterministic 1-, 10-, and 50-pane continuously-updating load scenarios with
  one and multiple attached UIs.
- Record request rate, CPU, update latency, topology latency, socket failures, and
  recovery behavior before changing the transport.

### Phase 1: UI Session coordinator

- Move terminal transport ownership out of `RemoteTerminalHost` into one coordinator
  owned alongside `RemoteSessionClient`.
- Keep `RemoteTerminalProjection` per pane; only polling, command dispatch, and
  recovery ownership move.
- Register and unregister pane consumers by terminal ID and visibility generation.
- Route decoded events to pane mailboxes and coalesce all ready projections into one
  UI wake.
- Retain legacy per-pane transport behind capability negotiation during migration.

### Phase 2: Batched Session polling

- Add `session.poll` with topology revision, agent revision, and a map of subscribed
  terminal generation/sequence cursors.
- Return topology, agent, and terminal changes within bounded per-channel and global
  payload budgets.
- Reuse existing terminal subscriber queues, event encoding, sequence validation, and
  snapshot resynchronization.
- Poll once per UI, initially at the current active 25-ms presentation cadence.
- Add adaptive idle backoff, while waking or returning to the active cadence promptly
  after input or output.
- Keep mutations on existing request/response control methods in this phase.

This changes steady request load from O(visible panes) to O(attached UIs) and is the
first release-worthy fix.

### Phase 3: Persistent server-to-UI event stream

- **Delivered.** Add a dedicated asynchronous Session event endpoint rather than holding a worker in
  the existing synchronous control listener pool.
- Keep one persistent authenticated stream per UI.
- Push an event batch when terminal output, topology, agents, or heartbeat state
  changes; send heartbeats during idle periods.
- Reuse the Phase 1 coordinator and Phase 2 batching, fairness, cursor, and snapshot
  logic.
- Retain `session.poll` as fallback for older servers or failed stream negotiation.

The delivered `session-stream-v1` handshake uses the short control endpoint only to
open an epoch-qualified stream and issue a one-use authenticated ticket. Registration
and cursor Updates plus Events/Heartbeat/Error frames use the persistent framed local
connection. The server derives the Phase 2 scheduler payload budget from the
negotiated writer queue, so one initial multi-pane snapshot cannot overflow its own
stream. Commands remain on short control methods until Phase 4.

### Phase 4: Bidirectional multiplexing

- Carry commands over the persistent connection with request IDs and correlated
  responses.
- Prioritize input, resize, topology commands, acknowledgements, and heartbeats over
  bulk presentation output.
- Retain the short-lived control endpoint for bootstrap, status, diagnostics, CLI
  access, and compatibility rather than normal attached-UI traffic.
- Remove legacy per-pane UI polling only after cross-version fallback and recovery are
  proven.

### Phase 5: Interruption UX

- Do not toast on the first retryable transport failure.
- Keep the last valid projections visible while reconnecting.
- Show a Session-disconnected warning only after a sustained outage, tentatively two
  to three seconds.
- Clear the warning after recovery without emitting repeated success toasts.
- Expose short interruptions, reconnect count, and resync reasons in diagnostics.

## Validation

- Protocol integration: multiplex two terminals, topology, and agents; prove each
  channel remains ordered independently.
- Fairness: continuously flood one terminal and prove topology, commands, and another
  terminal stay within their latency budgets.
- Backpressure: stop reading from one UI, exceed its queue budgets, and prove bounded
  memory plus channel-local snapshot recovery.
- Reconnect: drop the stream at each frame boundary, reconnect with cursors, and prove
  replay or authoritative snapshot convergence without duplicated mutations.
- Epoch replacement: restart the server and prove the client discards old-epoch
  events, re-handshakes, and converges.
- Visibility: suspend and resume dashboard panes while output continues; prove hidden
  panes receive no presentation delivery and resume through one current snapshot.
- Compatibility: new UI to old server and old UI to new server both retain supported
  behavior.
- Cross-platform: exercise Unix-domain sockets on macOS and named pipes on Windows,
  including cancellation and shutdown.
- CLI coexistence: sustained attached-UI traffic must not starve status, topology, or
  pane-control CLI requests.

## Acceptance Criteria

For one UI with ten visible continuously-updating panes:

- one Session event connection or one batched polling worker, never one poller per
  pane;
- request count does not grow linearly with pane count;
- no `Shared topology unavailable` toast or control `io_error` during normal load;
- terminal presentation latency below 50 ms at the 95th percentile;
- topology and interactive command latency remain below 100 ms at the 95th percentile;
- one noisy terminal cannot starve another logical channel; and
- reconnect either resumes ordered delivery or performs a clean bounded snapshot
  resynchronization.

For fifty visible panes across multiple UIs:

- bounded memory for every client and channel;
- no control endpoint starvation;
- graceful slow-client degradation;
- automatic recovery across server replacement; and
- no terminal process restart, controller loss, or scrollback loss caused solely by
  event-transport reconnection.

## Rollout and Rollback

Ship each new transport behind capability negotiation and keep the previous transport
available for at least one compatibility window. Metrics identify the selected path
and fallback reason. A stream-specific failure falls back to `session.poll`; it must
not silently return to per-pane polling when batched polling is supported.

The persistent transport is considered the default only after the dashboard and
multi-UI stress scenarios pass on both Windows and macOS. The existing control endpoint
remains available for recovery and CLI administration even after attached UIs use one
bidirectional Session connection.
