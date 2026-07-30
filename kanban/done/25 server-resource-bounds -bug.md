# Bound server memory and serialization cost globally

**Type:** bug
**Priority:** P2 / sequence 25
**Raised by:** Claude (server/client runtime review, 2026-07-30)
**Evidence:** [plans/reviews/2026-07-30-server-client-runtime-review.md](../../plans/reviews/2026-07-30-server-client-runtime-review.md#resource-bounds)

## Goal

Resource accounting is per-object but never global: per-tab pane caps, per-queue event caps,
and per-request size caps all exist, yet nothing bounds the totals. A looping client or a
large restored Session can exhaust the per-user server.

## Observed behaviour

**No global terminal cap, with eager allocation per terminal.**
`ServerTerminalRuntime`'s constructor calls `scrollback_.resize(80)`
(`libs/draxul-server/src/server_terminal_runtime.cpp:36-37`), which assigns
`capacity_ * cols` cells (`libs/draxul-terminal-core/src/scrollback_buffer.cpp:112`). At the
server default of 10,000 lines and ~44 bytes per `Cell` that is roughly 35 MB at 80 columns and
~140 MB after a 320-column resize — allocated at **endpoint registration**, before any process
spawns or any client attaches. Topology limits permit 128 × 256 × 256 `ServerTerminal` panes
per Session, and neither `create_server_terminal_with_id`
(`libs/draxul-server/src/server_kernel.cpp:379-452`) nor `TopologyService::apply` checks a
total. A client looping on `SplitPane`/`CreateTab` exhausts memory in seconds; a cold restore
preallocates for every saved pane.

**Event queues bounded by count, not bytes.** `broadcast` caps at
`kRemoteTerminalQueueLimit` (32) events
(`libs/draxul-server/src/remote_terminal_service.cpp:196-206`), but each queued event embeds a
full `TerminalDirtySnapshot`, and the kernel's own comment notes resize deltas "can be several
MiB apiece" (`server_kernel.cpp:42-44`). A client alive on topology polls but not polling a
terminal can transiently hold 32 × several MiB per terminal.

**The idempotency cache stores snapshots it never reads.** `command()` remembers a full
`snapshot_` copy per command (cap 2048, `libs/draxul-server/src/topology_service.cpp:499-503`),
but the duplicate path immediately overwrites the stored snapshot with the current one
(`:479-482`), so the copies are never observable. An interactive ratio drag submits a stream of
`SetSplitRatio` commands, so a large topology quickly pins 2048 full snapshots per Session.

**Redundant serialization.** Every delta and snapshot is JSON-encoded and dumped once purely
for a metrics byte counter (`remote_terminal_service.cpp:143`, `:157` — also covered by
`kanban/pending/12`), and queued events are re-encoded on every poll attempt (`:311-324`).
Encoding is also fatter than it needs to be: colours serialize as four-float JSON arrays and
hyperlink URIs repeat per cell rather than being deduplicated like attrs
(`libs/draxul-protocol/src/remote_terminal_protocol.cpp:34-50`, `:115-121`) — which is what
makes it plausible for a legitimate full-screen truecolour render to exceed the 8 MiB frame cap
with no degradation path. The `poll` budget check always admits the first event regardless of
size (`:317-321`), and the resync path bypasses the budget entirely (`:291-296`), so a single
oversized event makes `write_frame` refuse the frame (`control_plane.cpp:453`) and the client
re-requests it forever.

On the client, every published update rebuilds the whole grid
(`libs/draxul-host/src/remote_terminal_host.cpp:625-658`), and `Grid::clear()` sets
`full_dirty_`, discarding the precise dirty-cell information the protocol worked to deliver.

## Implementation

- [x] Add a server-wide maximum terminal count, checked in `create_server_terminal_with_id` and
      on restore, with a clear error.
- [x] Allocate scrollback lazily on first process start rather than at construction.
- [x] Enforce a shared server-wide scrollback cell reservation across terminal starts and
      resizes, release it with each runtime, and expose reserved/limit cells in server status.
- [x] Bound subscriber queues by bytes as well as count; coalesce a queued oversized delta into
      `needs_resync` instead of queueing it.
- [x] Make the poll budget cover the first event and the resync path, and give an oversized
      event a degradation path (strip hyperlinks/attrs, or chunk) rather than an unsendable
      frame.
- [x] Store only `{applied, duplicate}` in the command cache and rebuild the response from the
      live `snapshot_`.
- [x] Deduplicate hyperlink URIs into a table like attrs, and encode colours as packed RGBA
      integers.
- [x] Publish `(snapshot, dirty-list)` pairs to the client `Grid` and apply deltas
      incrementally; full-rebuild only on resize or a full event.
- [x] Retain unconsumed dirty updates until the UI applies them; coalesce overlapping worker
      publications into a full update from the latest projection.
- [x] Bound wire-visible titles and shell marks, and prune marks invalidated by a resize.

## Unit tests

- [x] A client looping on split commands hits the terminal cap with an error rather than
      exhausting memory.
- [x] Registering a terminal without starting it does not allocate full scrollback.
- [x] A subscriber holding large resize deltas is byte-bounded and flips to resync.
- [x] A full-screen truecolour frame with per-cell hyperlinks either fits the frame budget or
      degrades, and never wedges the client.
- [x] A ratio-drag storm does not grow the command cache past its byte bound.

## Acceptance criteria

- [x] No client-reachable operation can grow server memory without bound.
- [x] No legitimate terminal content produces an unsendable frame.
- [x] Metrics report the new bounds and any degradation applied.
- [x] Windows Release/Ninja build, full `ctest`, smoke, and render snapshots pass.
- [ ] macOS full build, `ctest`, smoke, and render snapshots pass.

## Dependencies and ownership

Shares `remote_terminal_service.cpp` with `kanban/done/12
server-crash-on-invalid-utf8 -bug.md` (which removes the metrics-only `dump()`); land `12`
first. The encoding changes here close the third entry in the plan's "Known boundaries retained
for later work" and are prerequisites for Slice 10, where the JSON tax becomes user-visible.

## Progress notes

Implemented on Windows Release/Ninja on 2026-07-30. The server now caps terminal
allocation at 256 (with a lower injectable test bound), allocates scrollback only after a
successful process spawn, and bounds each subscriber at 32 events and 2 MiB. Events are
encoded once and shared across subscriber queues. Resync/attach snapshots respect the control
frame budget and deterministically shed hyperlinks, then attributes, while preserving text
and geometry; sanitized metrics expose queue bytes, limits, oversize resyncs, and degraded
frames.

The compact wire format uses packed RGBA8 colours and a hyperlink table, so the server protocol
major is now 2 rather than silently presenting an incompatible 1.0 endpoint. Client projection
publishes dirty lists and `RemoteTerminalHost` applies them incrementally, rebuilding only for
full/resize/scrollback presentation transitions. Focused validation passed 39 assertions in
four terminal-bound cases, 2,188 assertions in two topology-cache cases, 27 assertions in
seven codec cases, and 1,249 assertions in eleven remote-host cases. Cross-platform full-suite
validation remains outstanding.

Follow-up hardening reserves `scrollback_capacity * current_columns` against a shared
24,000,000-cell server budget before process start or resize and releases the reservation when
the runtime is destroyed. Server status and `--server-status` report current/maximum reserved
cells. Terminal metadata now caps titles at 4 KiB on a UTF-8 boundary, keeps at most 1,024 shell
marks, shifts or drops them with live-grid scrolling, and drops marks outside a resized grid.
Scrollback resize has a strong allocation guarantee and reserves its old-plus-new peak against
the shared budget. Real runtime restart clears the prior process's grid and metadata before
publishing the next generation; an empty restarted title restores the client window to
`Draxul`. A terminal-buffer allocation failure safely releases scrollback storage and its
budget reservation instead of retaining mismatched grid strides. Worker publications preserve
unconsumed grid updates, and fake-terminal restart now resets its terminal state before the
generation resync.
Windows Release/Ninja validation passed the app build, direct smoke, 2,243 assertions in nine
resource-bound cases, 30 terminal-core assertions, 1,261 remote-host assertions, 480
remote-terminal assertions, and both focused recovery regressions.

The integrated Windows gate then passed every one of the 22 CTest groups, including the four
core shards, two app shards, five render comparisons, app smoke, and every optional product
module. The two load-sensitive server-recovery cases and their complete core shard also passed
in isolated reruns after a parallel-load timing failure. macOS execution remains delegated to
CI.
