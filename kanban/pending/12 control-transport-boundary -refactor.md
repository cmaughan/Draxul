# Split control common logic from platform transport

**Type:** refactor  
**Priority:** P1  
**Raised by:** Claude and Codex  
**Depends on:** pending `09` and `10` completion/freeze

## Boundary verification

- [ ] Inventory every Win32/POSIX helper, deadline, metadata/cache, framing, listener, and client responsibility.
- [ ] Record behavioral differences between the two server run loops before moving code.
- [ ] Pin public `control_plane.h` API and security/shutdown invariants.
- [ ] Split CLI/App cases from transport cases in the test inventory.

## Implementation and migration

- [ ] Extract common codec/deadline helpers without behavior change.
- [ ] Extract metadata/cache helpers.
- [ ] Add private platform-selected Win32 and POSIX transport sources.
- [ ] Move client exchange branches, then listener loops.
- [ ] Consolidate common loops only where recorded behavior is identical.
- [ ] Add `draxul-control-test-internals`.

## Unit tests

- [ ] Test frame limits, malformed JSON, depth, absolute deadlines, and partial I/O without live sockets.
- [ ] Test stale metadata, concurrent ownership, stop-pending, and listener recovery.
- [ ] Build `draxul-control` and `draxul-test-core`; run CTest label `core`.

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
- [ ] Focused transport tests no longer require `draxul-app`.
- [ ] Both platform suites and smoke remain green.
