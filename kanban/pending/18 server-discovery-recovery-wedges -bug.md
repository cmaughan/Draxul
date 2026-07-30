# Make server discovery and forced recovery work when the server is broken

**Type:** bug
**Priority:** P1 / sequence 18
**Raised by:** Claude (server/client runtime review, 2026-07-30)
**Evidence:** [plans/reviews/2026-07-30-server-client-runtime-review.md](../../plans/reviews/2026-07-30-server-client-runtime-review.md#recovery-mechanisms-that-depend-on-what-they-recover)

## Goal

Every recovery mechanism currently depends on the thing it is recovering. A crashed server,
a recycled PID, or a leaked marker can leave the user unable to start Draxul at all, with no
remedy short of deleting files by hand.

## Observed behaviour

**`force_stop` needs a healthy server.** It calls `status()` first and bails when that fails
(`libs/draxul-client/src/server_client.cpp:439-446`). A wedged main loop makes `dispatch()`
return `main_thread_timeout`, so `--force-stop-server --yes` prints "Draxul did not process
the request in time" and never reaches `TerminateProcess`/`kill` — in exactly the case the
command exists for.

**Liveness is a bare PID check.** `process_is_alive` (`server_client.cpp:51-76`) only asks
whether *some* process holds that number — no image name, no start time, no epoch
cross-check. `unavailable_result` maps a valid-metadata-plus-live-PID to `Busy`
(`:140-149`), and `ensure()` only launches for `Absent`/`Crashed`/`Stale` (`:316-318`,
`:342-345`). So after a crash whose PID gets recycled, every launch probes, classifies `Busy`,
spins the full 10 s timeout, and fails — permanently, until the metadata file is removed.

**Starting markers never age out.** `publish_starting_marker` writes
`server-starting-<pid>.json` (`libs/draxul-server/src/server_kernel.cpp:328-348`) and the only
cleanup is `remove_all_starting_markers()` on a *successful* start (`:1048`) or
`remove_starting_marker()` on the failure paths. A SIGKILL between marker publication and
endpoint claim leaves it behind; once that PID is recycled, `inspect_runtime` reports
`live_starting_process` and `unavailable_result` returns `Starting`
(`server_client.cpp:104-139`), which `ensure()` also never launches for.

**Version mismatch is checked in three places with three outcomes.** The transport envelope
returns `unsupported_version` (`libs/draxul-control/src/control_plane.cpp:199-201`),
`read_metadata` rejects a mismatched metadata version (`:302-311`) while `inspect_runtime`
reads the same file and only looks for `server_pid`, and `server.hello` returns
`incompatible_protocol`. Only the third maps to `ServerProbeState::Incompatible`
(`server_client.cpp:261-268`); the other two degrade into `Busy` and a 10 s hang, so a
mixed-version rollout reports the wrong problem.

## Implementation

- [ ] Record process start time (or a boot-relative token) in the server metadata alongside
      `server_pid`, and compare it in `process_is_alive`. This is the single change that makes
      `Busy`/`Crashed`/`Starting` classification sound.
- [ ] Treat an unreachable endpoint as authoritative over PID liveness after a short grace
      period, so a `Busy` classification cannot persist against a dead endpoint.
- [ ] Stamp starting markers with a creation time and expire them after a few seconds; delete
      markers whose PID is dead during `inspect_runtime` rather than only classifying them.
- [ ] Give `force_stop` a filesystem-only fallback: when `status()` fails, use the metadata
      `server_pid` plus the recorded start time, and keep requiring the explicit confirmation
      the caller already passes.
- [ ] Map `unsupported_version` and a metadata version mismatch to
      `ServerProbeState::Incompatible` in `unavailable_result`, so version problems report as
      version problems.
- [ ] Treat repeated listener recreation failure as fatal rather than warning forever: today
      `take_listener_error()` only logs (`server_kernel.cpp:2640-2646`) while `running()` stays
      true and the metadata stays published, so a server whose listeners all fail still reports
      "ready" while every client sees `endpoint_unavailable`.

## Unit tests

- [ ] Metadata naming a live PID that is *not* a Draxul server classifies as `Crashed` and
      `ensure()` relaunches.
- [ ] A starting marker older than the expiry no longer wedges `ensure()` in `Starting`.
- [ ] `force_stop` succeeds against a server whose control loop does not respond.
- [ ] A protocol-version mismatch reports `Incompatible` rather than timing out as `Busy`.
- [ ] Repeated listener failure stops the kernel so a client can classify it as `Crashed`.

## Acceptance criteria

- [ ] No crashed-server state requires the user to delete files by hand to start Draxul.
- [ ] `--force-stop-server --yes` works against a wedged server.
- [ ] Version skew produces an accurate message immediately, not a 10 s hang.
- [ ] Full build, `ctest`, and smoke pass on both platforms.

## Dependencies and ownership

Shares `server_client.cpp` and the runtime-metadata schema with
`kanban/pending/16 posix-server-singleton-race -bug.md`; if both are scheduled, one owner
should hold the metadata format, since the start-time field added here and the lock file added
there are the same discovery contract. The concurrent-launch fixtures from `16` are reused for
the PID-reuse tests.
