# Split the current control endpoint from platform transport

**Type:** refactor
**Priority:** P1
**Raised by:** Claude and Codex
**Prerequisites completed:** `kanban/done/09 macos-remote-terminal-channel -bug.md`
and `kanban/done/10 server-lifecycle-sigterm-eviction -bug.md`

## Relationship to the multiplexed Session transport

This card is the behavior-preserving preparation for
`kanban/pending/40 multiplexed-session-event-stream -feature.md`. It owns the
existing short-lived synchronous control endpoint: common framing, deadlines,
staged transport errors, metadata, and private Windows/POSIX client/listener
implementations.

It does **not** add `session.poll`, capability negotiation, a persistent event
endpoint, bidirectional multiplexing, or UI Session coordination. Those belong to
card 40. Card 40's diagnostics baseline, UI coordinator, and bounded
`session.poll` request may land alongside this refactor through the unchanged public
`ControlClient` API. Card 12 still owns the reusable framing/deadline/error boundary
needed before a persistent endpoint; that endpoint also waits for the server ownership boundaries in
`kanban/pending/13 server-kernel-private-decomposition -refactor.md`.

## Boundary verification

- [ ] Inventory every Win32/POSIX helper, deadline, metadata/cache, framing, listener, and client responsibility.
- [ ] Record behavioral differences between the two server run loops before moving code.
- [ ] Pin public `control_plane.h` API and security/shutdown invariants.
- [ ] Split CLI/App cases from transport cases in the test inventory.

## Implementation and migration

- [ ] Extract common codec/deadline helpers without behavior change.
- [ ] Preserve the failing transport stage and native platform error in an internal
      typed result; keep the current public result mapping unchanged in this card.
- [ ] Extract metadata/cache helpers.
- [ ] Add private platform-selected Win32 and POSIX transport sources.
- [ ] Move client exchange branches, then listener loops.
- [ ] Consolidate common loops only where recorded behavior is identical.
- [ ] Add `draxul-control-test-internals`.

## Unit tests

- [ ] Test frame limits, malformed JSON, depth, absolute deadlines, and partial I/O without live sockets.
- [ ] Test stale metadata, concurrent ownership, stop-pending, and listener recovery.
- [ ] Build `draxul-control` and the owning core test target; run the focused CTest selection.

## Cross-platform validation

- [ ] Windows: ACLs, named-pipe ownership, overlapped cancellation, and abandonment.
- [ ] macOS: locks, socket ownership, accepted-fd blocking mode, `EINTR`, and shutdown.
- [ ] Compare timeout/error/reconnect behavior across platforms.

## Agent documentation/tooling

- [ ] Add a local dependency/transport contract comment.
- [ ] Update `docs/module-map.md` without exposing backend types.

## Acceptance criteria

- [ ] Public API and callers are unchanged.
- [ ] Platform code is absent from common transport sources.
- [ ] Common framing, deadline, cancellation, and error contracts can be reused by
      card 40 without exposing platform backend types publicly.
- [ ] No long-lived request consumes a synchronous control listener worker.
- [ ] Focused transport tests no longer require `draxul-app`.
- [ ] Both platform suites and smoke remain green.
