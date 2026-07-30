# Close the POSIX race that can start two servers for one runtime

**Type:** bug
**Priority:** P0 / sequence 16
**Raised by:** Claude (server/client runtime review, 2026-07-30)
**Evidence:** [plans/reviews/2026-07-30-server-client-runtime-review.md](../../plans/reviews/2026-07-30-server-client-runtime-review.md#4-posix-singleton-enforcement-has-a-toctou)

## Goal

One runtime directory means one server, on both platforms, even when two UIs launch
simultaneously against a crashed server's leftovers. Windows gets this from a kernel
primitive; POSIX currently gets an advisory sequence with no atomicity.

## Observed behaviour

`libs/draxul-control/src/control_plane.cpp:867-889` probes the socket for liveness, then
unlinks and binds, with nothing held between the check and the use:

```cpp
if (::access(endpoint.c_str(), F_OK) == 0) {
    ... in_use = ::connect(probe, ...) == 0; ...
    ::unlink(endpoint.c_str()); // stale: the owner is gone
}
if (::bind(server, ...) != 0 || ::chmod(...) != 0 || ::listen(server, 4) != 0)
```

With a stale socket from a crashed server and two UIs calling `ensure()` at once: both
`access()` succeed, both probe-connect fail, both `unlink`. Server A binds, listens, and
publishes metadata. Server B then unlinks **A's now-live socket**, binds its own, and
overwrites the metadata with its own token and endpoint. `bind()` succeeded for both, so
neither sets `endpoint_in_use` and `ServerKernel::start()` returns `Started` twice. Clients
reach only B; A is an unreachable orphan still holding PTY processes.

A narrower variant: if A binds between B's `access()` and B's `bind()`, B fails with
`EADDRINUSE` and reports "Unable to bind the control socket" without setting
`endpoint_in_use`, so `ServerKernel::start` returns `Failed` rather than `AlreadyRunning` — a
misreported but benign outcome.

`kanban/pending/09 multi-instance-session-endpoint-collisions -bug.md` fixed the case where a
*client* unconditionally unlinks a live server's socket. This is the different case of two
*launchers* racing on a stale one, which the probe-then-unlink design cannot resolve.

## Implementation

- [x] Replace probe-then-unlink with a real lock: `O_CREAT|O_EXCL` lock file plus
      `flock`/`fcntl`, held for the process lifetime, covering the stale-socket reclaim, the
      bind, and the metadata publish. Only unlink a stale path while holding the lock.
- [x] Map "another process holds the lock" to `endpoint_in_use` so `ServerKernel::start`
      reports `AlreadyRunning`, matching Windows.
- [x] Make `EADDRINUSE` from `bind` set `endpoint_in_use` as well, so the narrow variant is
      reported honestly.
- [ ] Consider the same lock file on Windows for symmetry. `FILE_FLAG_FIRST_PIPE_INSTANCE`
      already gives the guarantee there, so this is optional — but one mechanism is easier to
      reason about than two with different strengths.
- [x] Use `weakly_canonical` in `normalized_runtime_key` (`control_plane.cpp:81-97`). Today a
      symlinked or differently-spelled `--server-runtime-dir` hashes differently and yields a
      second "singleton" with its own endpoint and metadata in the same directory.

## Unit tests

- [x] Two `ensure()` calls racing against a stale socket produce exactly one server; the loser
      reports `AlreadyRunning` and attaches to the winner.
- [x] A stale socket with no live owner is still reclaimed by a single launcher (must not
      regress `09`'s stale-endpoint recovery test).
- [x] A live server's socket and metadata are never removed or overwritten by a losing
      launcher (extends `09`'s incumbent-intact test to the concurrent case).
- [x] A symlinked runtime directory resolves to the same endpoint as its target.

## Acceptance criteria

- [ ] Launching two UIs simultaneously against a crashed server yields one server and no
      orphaned PTY owner.
- [x] Every "someone else owns this" outcome reports `AlreadyRunning`, never `Failed`.
- [x] macOS: socket path, `0600`/`0700` permissions, and `sun_path` length handling preserved.
- [x] Windows: named-pipe first-instance semantics and SDDL unchanged.
- [ ] Full build, `ctest`, and smoke pass on both platforms.

## Dependencies and ownership

Extends `kanban/pending/09 multi-instance-session-endpoint-collisions -bug.md`; one owner
should hold both, since the stale-endpoint policy must stay consistent. Independent of the
other P0 cards. The concurrent-launch test fixtures added here are also what
`kanban/pending/18 server-discovery-recovery-wedges -bug.md` needs.
