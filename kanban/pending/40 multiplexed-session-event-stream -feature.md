# Multiplex client/server Session communication

**Type:** feature
**Priority:** P1 / sequence 40
**Design:** `plans/session-event-stream.md`
**Depends on by phase:** `kanban/pending/12 control-transport-boundary -refactor.md`
and `kanban/pending/13 server-kernel-private-decomposition -refactor.md`

## Goal

Replace per-pane short-lived polling with one multiplexed Session transport per
attached UI. The final path is one persistent bidirectional connection carrying
terminal presentation, topology, agents, commands, responses, heartbeats, and
recovery over independently ordered logical channels.

`session.poll` is the first release-worthy milestone and remains the negotiated
fallback. The existing short-lived control endpoint remains available for bootstrap,
status, diagnostics, CLI access, and compatibility.

## Boundary and sequencing

- `kanban/pending/12 control-transport-boundary -refactor.md` owns the current
  synchronous endpoint's reusable framing, deadlines, cancellation/error contracts,
  and private Windows/POSIX backends. It does not implement this feature.
- Diagnostics/load baselines and the UI Session coordinator may land alongside card
  12 because they do not change the wire transport.
- Batched `session.poll` is an immediate bounded request through the unchanged public
  control API, so it may land alongside card 12. Card 12 still owns the reusable
  framing, deadline, cancellation, and staged-error internals; the batch service does
  not reach into or duplicate those platform backends.
- The persistent endpoint and per-UI writer lifecycle also wait for the private
  ownership split in
  `kanban/pending/13 server-kernel-private-decomposition -refactor.md`.
- Do not implement per-pane long polling on the four-worker synchronous control
  endpoint. It recreates the starvation this card is intended to remove.

## Phase 0 — diagnostics and baseline

- [x] Preserve operation, transport stage, and native platform errors instead of
      collapsing every failure into `io_error`.
- [x] Record connection attempts, listener occupancy, request counts by method,
      queue/dispatch/response time, and failures by transport stage.
- [x] Add deterministic 1-, 10-, and 50-pane continuously-updating scenarios with
      one and multiple attached UIs.
- [x] Capture request rate, CPU, presentation/topology latency, failures, and
      recovery behavior before changing transport ownership.

### Delivered checkpoint — legacy transport load fixture

`tests/session_transport_load_tests.cpp` adds a hidden
`[.session-load][control][remote-terminal]` integration fixture. It exercises the
real control endpoint with production terminal clients/services and deterministic
fake terminal runtimes, emits JSON to stdout (and optionally to
`DRAXUL_SESSION_LOAD_REPORT`), and treats saturation failures as baseline data. A
bounded sequential recovery pass must still converge every terminal projection.

The initial Windows Debug baseline on 2026-08-17 completed all 1,536 assertions:
1 and 10 panes had no request failures; 50 panes across two UIs recorded 133
`endpoint_unavailable` failures and recovered every projection. Terminal delivery
p95 increased from 9.8 ms at one pane to 129.2 ms at ten panes and 970.1 ms at
fifty panes; topology p95 reached 635.9 ms at fifty panes. These measurements are
diagnostic evidence for the coordinator/multiplex work, not timing gates for CI.

### Delivered checkpoint — bounded control diagnostics

The extracted control transport now retains typed operation, stage, native error
domain/code, and public compatibility classification at the point of failure on
both client and server paths. Additive bounded snapshots record client attempts,
metadata refreshes, listener capacity/current/peak occupancy, accepted connections,
requests and failures, per-method queue/dispatch/response timing, and transport
failure buckets. `ControlClientResult` keeps its compatibility mapping; hidden
failures from a successful fresh-metadata retry remain observable in diagnostics.

`server.status` exposes a transport-neutral copy under optional
`control_transport`; older producers remain valid when the field is absent. The
load fixture includes the same snapshot in its JSON report, so later phases can
compare connection pressure and failure stages rather than infer them only from
request outcomes.

## Phase 1 — one UI Session coordinator

- [x] Move polling, command dispatch, and recovery ownership out of each
      `RemoteTerminalHost` into one coordinator owned beside `RemoteSessionClient`.
- [x] Keep one `RemoteTerminalProjection` per pane and register consumers by terminal
      ID plus visibility generation.
- [x] Route decoded updates into pane mailboxes and coalesce ready projections into
      one UI wake.
- [x] Retain capability-selected legacy per-pane clients during migration.

### Delivered checkpoint — UI Session coordinator

`RemoteSessionCoordinator` now owns production remote-terminal registrations,
legacy client workers, bounded command queues, recovery, suspension, scrollback,
projection mailboxes, visibility generations, and coalesced UI wakes. `App` owns one
coordinator beside `RemoteSessionClient`, drains every active and background pane
before acknowledging a wake, and stops all entries against one shared shutdown
deadline. `RemoteTerminalHost` is a main-thread presentation adapter on this path;
the experimental fake transport and caller-supplied host factories retain the
legacy per-host implementation as the migration fallback.

Focused coverage lives in `tests/remote_session_coordinator_tests.cpp`, the
coordinator-backed render/control-transfer case in
`tests/remote_terminal_host_tests.cpp`, and the composition precedence case in
`tests/pane_manager_tests.cpp`. The wire protocol and request count are deliberately
unchanged in this phase; `session.poll` remains Phase 2.

## Phase 2 — batched Session polling

- [x] Add `session.poll` with topology/agent revisions and subscribed terminal
      generation/sequence cursors.
- [x] Return bounded, fairly scheduled topology, agent, and terminal changes with
      channel-local snapshot resynchronization.
- [x] Poll once per attached UI, with active cadence and adaptive idle backoff.
- [x] Keep mutations on existing request/response control methods in this phase.
- [x] Negotiate `session-poll-v1` and fall back to legacy clients only for servers
      that do not support batched polling.

### Delivered checkpoint — bounded Session polling

The server now negotiates `session-poll-v1` and serves one immediate, authenticated
batch containing topology, agent, and per-registration terminal channels. The private
Session scheduler applies a hard response budget, per-terminal soft quantum, rotated
fairness, independent duplicate-terminal subscriptions, visibility generations, and
channel-local snapshot recovery over the existing bounded terminal subscriber queues.

When negotiated, `RemoteSessionCoordinator` owns one recurring batch worker for the
UI and externally feeds topology/agent snapshots into `RemoteSessionClient`; terminal
input, resize, control, scrollback, topology mutations, and status remain ordinary
short requests. Idle polls back off from 25 to 50 to 100 ms and new work wakes the
worker. Missing capability keeps the Phase-1 workers; a server that advertises but
rejects the method falls back once, while transient failures remain on the batch path.

The deterministic Windows Debug load fixture converges all projections and records
exactly 20, 20, and 40 recurring `session.poll` requests for 1/1, 10/1, and 50/2
pane/UI scenarios, versus 60, 240, and 1,080 equivalent legacy requests. Batched mode
issues zero recurring `terminal.poll`, `topology.poll`, or `agent.poll` requests.

## Phase 3 — persistent server-to-UI events

- [x] Add a dedicated asynchronous Session event endpoint; do not hold a synchronous
      control listener worker for the lifetime of the UI.
- [x] Keep one authenticated stream per UI, bound to client ID, Session ID, server
      epoch, negotiated budgets, subscriptions, and cursors.
- [x] Push fair bounded batches for terminal output, topology, agents, and heartbeat
      state; the server state thread never waits for platform I/O.
- [x] Resume from cursors where possible and snapshot only channels that cannot
      resume after a gap, generation change, overflow, or epoch replacement.
- [x] Negotiate `session-stream-v1` and retain `session.poll` as fallback.

### Delivered checkpoint — persistent Session events

The server now owns one epoch-qualified, current-user-only asynchronous stream
listener, separate from the four synchronous control workers. An authenticated
`session.stream.open` request issues a one-use ticket bound to client, Session,
epoch, subscriptions, and cursors. Each attached UI has independent reader/writer
workers, a bounded negotiated queue, a five-second write deadline, heartbeat lease
refresh, and off-state-thread connection reaping. The state thread reuses the Phase 2
fair batch scheduler with a payload budget derived from the negotiated writer queue;
it never waits for stream I/O or advances a cursor before the client acknowledges the
Events frame with an Update.

`RemoteSessionCoordinator` now selects Stream → `session.poll` → legacy. Its
reader only fills a bounded inbox; the existing coordinator worker remains the sole
owner of terminal projections and cursors, routes topology/agent snapshots, validates
epoch/frame/request ordering, coalesces subscription updates, and acknowledges every
flow-controlled Events frame. Terminal and topology mutations deliberately remain
short control requests for Phase 4.

The Windows Debug stream load converges 1, 10, and 50 panes with exactly one stream
per UI and zero recurring `session.poll`, terminal, topology, or agent poll requests.
At 50 panes/two UIs the largest frame was 917,942 bytes under a 1 MiB writer budget;
a deliberately stalled UI was disconnected after eight update rounds while the
healthy UI converged and `server.status` completed in 3.4 ms. macOS transport and
cancellation evidence remains pending remote CI.

## Phase 4 — bidirectional multiplexing

- [x] Carry commands on the persistent connection with request IDs, idempotency, and
      correlated responses.
- [x] Prioritize input, resize, topology commands, acknowledgements, responses, and
      heartbeats over bulk presentation traffic.
- [x] Bound bytes globally per UI and per logical channel; reserve control capacity
      and rotate fairly between busy terminals.
- [x] Negotiate `session-stream-commands-v1`; keep the short-lived endpoint for
      bootstrap, CLI, diagnostics, and compatibility.
- [x] Remove legacy per-pane polling from the negotiated Session path only after
      fallback and recovery are proven; retain it solely for old-server compatibility.

### Delivered checkpoint — bidirectional Session commands

The persistent Session connection now carries bounded terminal input, resize,
controller and scrollback requests, topology mutations, and attached-UI agent
start/restart operations. Every command has a stream correlation ID in addition to
its existing method-specific mutation ID. The server dispatches only an explicit
authenticated Session allowlist, caches completed results by client/Session/request,
replays identical retries after stream replacement, and rejects conflicting reuse
without redispatching.

Each UI has separate bounded command ingress and control/event writer queues. Reserved
control bytes and writer-time frame numbering allow command responses and heartbeats
to pass queued presentation while preserving contiguous wire order; bulk terminal
payloads still use the Phase 2 rotated scheduler. A command-capable client sends all
normal attached-UI mutations on the stream, requeues safe terminal/topology work onto
short control after stream failure, and retains `session.poll` before legacy fallback.
Bootstrap, CLI, status, diagnostics, and old-client/server compatibility remain on the
short control endpoint.

The Windows Debug command-stream fixture covers two terminal mutations, topology and
agent convergence, exact replay, conflicting-ID rejection, a lost response replayed
after stream replacement without redispatch, and real `ServerKernel` authentication
and Session binding. The hidden 50-pane/two-UI load completed 1,024 assertions: the
stalled UI was isolated after eight pressure rounds, reserved control capacity still
admitted its command response beside an approximately 831 KiB presentation frame, the
healthy UI converged, and no recurring terminal, topology, agent, or `session.poll`
request used the short control endpoint. macOS command-stream evidence remains pending
remote CI.

## Phase 5 — interruption UX and diagnostics

- [x] Keep last coherent projections visible through transient disconnects and do
      not toast on the first retryable failure.
- [x] Show one sustained-outage warning, clear it quietly after recovery, and expose
      reconnect/fallback/resync reasons in diagnostics.
- [x] A stream failure falls back to `session.poll`, never silently to per-pane
      polling when batched polling is available.

### Delivered checkpoint — coherent interruption UX

Client recovery now tracks one UI-scoped Session outage in addition to its existing
per-channel retry state. The first retryable failure is diagnostic only: existing
topology, agent, and terminal projections remain visible, and topology/agent errors do
not produce duplicate toasts. An outage becomes user-visible only after two seconds,
emits one background-reconnect warning, and clears quietly when any viable Session
transport resumes.

The coordinator exposes an immutable snapshot of the selected Stream → `session.poll`
→ legacy path and bounded reconnect, fallback, resynchronization, interruption, and
recovery reason counts. The diagnostics panel shows that snapshot together with the
short control transport's request, connection, metadata-refresh, and native-stage
failure counters. A failed stream remains on the single `session.poll` worker whenever
that capability is available; legacy workers start only when batched polling itself is
unsupported.

Deterministic coordinator acceptance starts with coherent stream-delivered terminal,
topology, and agent projections, forces EOF plus one failed fallback poll, and proves
the retained cursors, non-sustained first failure, two-second warning eligibility,
bounded reason metrics, quiet recovery, and zero legacy requests. The final Windows
Debug aggregate passed all nine jobs (1,609 C++ cases / 41,502 assertions plus 82
Python cases); Debug and Release smoke scenarios also passed. Cross-platform evidence
remains pending asynchronous CI.

## Validation

- [ ] Multiplex at least two terminals plus topology and agents; prove independent
      per-channel ordering and correlated command responses.
- [ ] Flood one terminal and prove another terminal, topology, commands, and
      heartbeats stay within their latency budgets.
- [ ] Stop one UI reading and prove bounded memory, channel-local snapshot recovery,
      and no effect on other clients or the server state loop.
- [ ] Drop and reconnect at frame boundaries; prove cursor resume or authoritative
      snapshot convergence without duplicated mutations.
- [ ] Restart the server; reject old-epoch events, re-handshake, and converge without
      restarting terminals or losing controller/scrollback state.
- [ ] Suspend/resume hidden panes and prove presentation stops while runtimes continue
      headlessly, then resumes through one current snapshot.
- [ ] Exercise new-UI/old-server and old-UI/new-server compatibility.
- [ ] Validate Unix-domain sockets on macOS and named pipes on Windows, including
      cancellation, bounded shutdown, and simultaneous CLI traffic.

## Acceptance criteria

- [x] One UI with ten active panes owns one event connection or one batched polling
      worker; request count does not grow linearly with pane count.
- [ ] Normal load produces no control starvation, `Shared topology unavailable`
      toast, or generic unexplained transport `io_error`.
- [ ] At the 95th percentile, terminal presentation is below 50 ms and topology plus
      interactive commands are below 100 ms in the defined load fixture.
- [ ] One noisy terminal cannot starve another channel; slow clients degrade without
      unbounded queues or blocking other clients.
- [ ] Fifty panes across multiple UIs retain bounded memory and recover automatically
      across transport interruption and server replacement.
- [ ] Reconnection alone never restarts a terminal process or loses controller,
      topology, or scrollback state.
