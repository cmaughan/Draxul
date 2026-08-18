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

- [x] Inventory every Win32/POSIX helper, deadline, metadata/cache, framing, listener, and client responsibility.
- [x] Record behavioral differences between the two server run loops before moving code.
- [x] Pin public `control_plane.h` API and security/shutdown invariants.
- [x] Split CLI/App cases from transport cases in the test inventory.

## Implementation and migration

- [x] Extract common codec/deadline helpers without behavior change.
- [x] Preserve the failing transport stage and native platform error in an internal
      typed result; keep the current public result mapping unchanged in this card.
- [x] Extract metadata/cache helpers.
- [x] Add private platform-selected Win32 and POSIX transport sources.
- [x] Move client exchange branches, then listener loops.
- [x] Consolidate common loops only where recorded behavior is identical.
- [x] Add `draxul-control-test-internals`.

## Unit tests

- [x] Test frame limits, malformed JSON, depth, absolute deadlines, and partial I/O without live sockets.
- [x] Test stale metadata, concurrent ownership, stop-pending, and listener recovery.
- [x] Build `draxul-control` and the owning core test target; run the focused CTest selection.

## Cross-platform validation

- [x] Windows: ACLs, named-pipe ownership, overlapped cancellation, and abandonment.
- [ ] macOS: locks, socket ownership, accepted-fd blocking mode, `EINTR`, and shutdown.
- [x] Compare timeout/error/reconnect behavior across platforms in the extracted implementation; confirm the POSIX suite on macOS CI.

## Agent documentation/tooling

- [x] Add a local dependency/transport contract comment.
- [x] Update `docs/module-map.md` without exposing backend types.

## Acceptance criteria

- [x] Public API and callers are unchanged.
- [x] Platform code is absent from common transport sources.
- [x] Common framing, deadline, cancellation, and error contracts can be reused by
      card 40 without exposing platform backend types publicly.
- [x] No long-lived request consumes a synchronous control listener worker.
- [x] Focused transport tests no longer require `draxul-app`.
- [ ] Both platform suites and smoke remain green.

## Implementation status — 2026-08-17

The common facade is now separated from the codec, deadline, metadata/cache, and
platform transport implementations. The public header and public error mapping
remain unchanged. Windows focused transport coverage is green, including current-user
security, the four-listener starvation case, fragmented syscall progress, and an
injected replacement-listener failure followed by successful recovery. The extraction also fixes two
ownership/cancellation hazards found during the inventory: a POSIX incumbent that
abandons its endpoint can no longer unlink a successor socket during shutdown, and a
pending Win32 `ConnectNamedPipe` is cancelled and drained before its `OVERLAPPED`
storage is released. Debug tests and smoke plus a clean Release smoke are green on
Windows. The card remains pending only for macOS CI proof.
