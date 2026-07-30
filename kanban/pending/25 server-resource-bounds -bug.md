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

- [ ] Add a server-wide maximum terminal count, checked in `create_server_terminal_with_id` and
      on restore, with a clear error.
- [ ] Allocate scrollback lazily on first process start rather than at construction.
- [ ] Bound subscriber queues by bytes as well as count; coalesce a queued oversized delta into
      `needs_resync` instead of queueing it.
- [ ] Make the poll budget cover the first event and the resync path, and give an oversized
      event a degradation path (strip hyperlinks/attrs, or chunk) rather than an unsendable
      frame.
- [ ] Store only `{applied, duplicate}` in the command cache and rebuild the response from the
      live `snapshot_`.
- [ ] Deduplicate hyperlink URIs into a table like attrs, and encode colours as packed RGBA
      integers.
- [ ] Publish `(snapshot, dirty-list)` pairs to the client `Grid` and apply deltas
      incrementally; full-rebuild only on resize or a full event.

## Unit tests

- [ ] A client looping on split commands hits the terminal cap with an error rather than
      exhausting memory.
- [ ] Registering a terminal without starting it does not allocate full scrollback.
- [ ] A subscriber holding large resize deltas is byte-bounded and flips to resync.
- [ ] A full-screen truecolour frame with per-cell hyperlinks either fits the frame budget or
      degrades, and never wedges the client.
- [ ] A ratio-drag storm does not grow the command cache past its byte bound.

## Acceptance criteria

- [ ] No client-reachable operation can grow server memory without bound.
- [ ] No legitimate terminal content produces an unsendable frame.
- [ ] Metrics report the new bounds and any degradation applied.
- [ ] Full build, `ctest`, and smoke pass on both platforms.

## Dependencies and ownership

Shares `remote_terminal_service.cpp` with `kanban/pending/12
server-crash-on-invalid-utf8 -bug.md` (which removes the metrics-only `dump()`); land `12`
first. The encoding changes here close the third entry in the plan's "Known boundaries retained
for later work" and are prerequisites for Slice 10, where the JSON tax becomes user-visible.
