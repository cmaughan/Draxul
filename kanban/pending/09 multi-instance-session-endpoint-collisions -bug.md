# Fix silent collisions between concurrent Draxul instances

**Type:** bug
**Priority:** P1 / sequence 09
**Raised by:** Claude (architecture review, 2026-07-27)

## Goal

Two Draxul processes sharing a session id currently corrupt each other's saved
topology and silently steal each other's control endpoint, with no warning on
either side. Make concurrent instances either safely isolated or explicitly
refused, and make control-endpoint failures observable.

This is a correctness bug, not an architecture change: sessions remain
file-backed and there is still no background owner.

## Observed behaviour

Default `session_id` is `"default"` (`app/cli_args.h:34`), so two bare launches
collide on everything:

- **Duplicate restore.** `session_id_exists()` (`app/session_id.cpp`) is a file
  existence test with no liveness component, so instance 2 restores and respawns
  the same panes instance 1 is already running.
- **Last-writer-wins topology.** Individual writes are atomic
  (`replace_session_state_file`, `app/session_state.cpp:1086-1111`) but nothing
  arbitrates between processes, so one instance's layout silently overwrites the
  other's.
- **Control-endpoint hijack (POSIX).** `::unlink(endpoint)` runs unconditionally
  before `bind` (`libs/draxul-control/src/control_plane.cpp:673`). Instance 2
  removes instance 1's live socket and binds its own; instance 1's server thread
  keeps polling an unlinked, unreachable socket.
- **Token overwrite.** The metadata file is rewritten with a fresh random token,
  so all `draxul` CLI traffic routes to the last starter only.
- **Windows nondeterminism.** `CreateNamedPipeW` is called without
  `FILE_FLAG_FIRST_PIPE_INSTANCE` and with `nMaxInstances = 4`
  (`control_plane.cpp:609-613`), so both instances create instances of the same
  pipe name and clients reach whichever answers.
- **Silent bind failure.** `ControlServer::start` sets `active = true`, spawns the
  thread, and returns `true` (`control_plane.cpp:522-524`) *before* the worker
  attempts bind. A genuine bind/listen failure only sets `active = false` on the
  worker, so the app runs on with a dead control plane and no diagnostic. The
  `App::initialize` error path therefore catches only pre-thread failures.

## Also found while verifying (2026-07-27)

- **A long runtime path stops Draxul from starting at all.** `sockaddr_un::sun_path`
  is 104 bytes on macOS. `<config>/runtime/<session_key>.sock` under a normal
  `~/Library/Application Support/draxul/` is fine, but the margin is thin, and
  `control_plane.cpp` correctly refuses to bind past the limit. The problem is
  what happens next: the control server's failure propagates out of
  `App::initialize()`, so a user with a long enough home path gets **no window
  at all** rather than a running Draxul without its automation endpoint. The
  control plane is an optional surface and must not be able to veto startup.
  (Found via `tests/control_plane_tests.cpp`, which hit exactly this under
  macOS's `$TMPDIR`; the test-side path budget is fixed, the product behaviour
  is not.)
- **The control-plane tests were timing-flaky, and the cause was the product,
  not the tests.** `[control]` alone produced 2 failed / all passed / 1 failed
  over three consecutive runs. The initial reading (wall-clock deadlines in the
  tests) was wrong: `start()` returned `true` after merely *spawning* the
  worker, so callers raced the thread that had not yet reached `bind()`.
  Making `start()` wait for the listener to be claimed removed the flakiness
  entirely — the full app suite is now 467/467 green on repeated runs.

## Decide the policy first

- [x] Choose the intended semantics for a second instance on the same session id.
      **Decided (2026-07-27): refuse with a clear message.** Sessions are
      single-owner; the second instance exits naming `--session <id>` and
      `--new-session` as the remedies.
- [x] Confirm whether distinct `--session` ids remain the only supported
      concurrency mode (they are — correctly isolated via `session_key()`).
- [x] **Decided: a control-endpoint failure is NOT fatal.** Only a *live
      endpoint owned by another instance* refuses startup; every other endpoint
      failure warns, toasts, and continues without the automation surface.

## Implementation

- [x] Replace unconditional `::unlink`-before-`bind` with a liveness check:
      probe-connect first; only unlink a provably stale endpoint. Preserves
      `3128a96e` (clients must never unlink a live server socket).
- [x] Add `FILE_FLAG_FIRST_PIPE_INSTANCE` on Windows (first create only) so a
      second server fails deterministically instead of racing. **Unverified —
      compiles only on Windows; needs a CI run.**
- [x] Surface bind/listen failure out of the worker thread: `start()` now waits
      on a startup latch and reports the real outcome.
- [x] Gate session restore on the policy — the Session is claimed *before*
      `initialize_chrome_host()`, so a refused instance never respawns panes.
- [x] Ensure the loser never rewrites the other's metadata or socket: the token
      is published only after the listener is owned, and `stop()` cleans up
      only when `owns_endpoint`.
- [x] Make control-server startup failure non-fatal: warn + toast and continue
      without an endpoint; only an in-use endpoint refuses startup.
- [x] The control-test flakiness is gone — it was the `start()` race, not the
      test deadlines (see above). Full suite green on repeated runs.

## Unit tests

- [x] Two servers on one session key: the second fails, reports
      `endpoint_in_use()`, and the incumbent keeps its socket and its token
      (`a second server refuses a live endpoint and leaves the incumbent intact`).
- [x] Stale-endpoint recovery: a socket path left by a dead owner is reclaimed
      (`a stale endpoint from a dead owner is reclaimed`, POSIX-only).
- [x] Bind failure is reported to the caller rather than silently disabling the
      control plane (covered by the startup latch; exercised by the above).
- [ ] Distinct session ids still start two fully independent servers.
- [ ] App-level: a second instance on a live session id refuses **without
      spawning panes**. The ordering is implemented, but asserting it needs a
      second in-process `App`, which the current fixtures do not support.

## Cross-platform validation

- [ ] macOS: unix-socket path, `0600`/`0700` permissions preserved.
- [ ] Windows: named-pipe first-instance semantics and SDDL unchanged.
- [ ] Note that `kanban/done/25` recorded Windows DACL assertions failing in
      Debug but passing in Release; re-check that path if touched.

## Acceptance criteria

- [ ] Launching Draxul twice cannot corrupt a saved session or silently steal a
      control endpoint.
- [ ] A failed control endpoint is always visible to the user or the log.
- [ ] `draxul agent list` deterministically addresses one known instance.
- [ ] Full build, `ctest`, and smoke pass on both platforms.

## Dependencies and ownership

Independent of the pending 00-08 CMake/library-boundary sequence; touches
`libs/draxul-control` and `app/` session paths only. One owner should hold both
the transport and the restore-gate change so the policy stays consistent.
