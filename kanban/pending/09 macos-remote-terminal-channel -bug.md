# Fix the macOS remote-terminal channel and server suite failures

**Type:** bug
**Priority:** P0 / sequence 09
**Raised by:** Claude (first macOS validation of codex/server-client-runtime, 2026-08-03)

## Goal

After the socket-path fix (`92c00687`), a plain macOS launch connects and
auto-spawns one healthy server — but the GUI's remote-terminal channel never
comes up: `Remote terminal interruption (io_error), recovery attempt N`
repeats indefinitely with backoff climbing to ~5s per attempt, while the
server log shows a healthy kernel that started its shell. The same subsystem
fails broadly in the unit suites on macOS. This worked on Windows; it is a
platform gap, not a regression from the startup fix.

## Observed behaviour (2026-08-03, clean single-server run)

- `server.hello` / `ensure` / `status` requests work — small request/response
  round-trips are fine.
- Terminal-channel requests fail persistently: 10+ recovery attempts, each
  dying around 4.3–5.0s. The 5s coincidence points at the transport's
  `kIoTimeout = 5s` (`control_plane.cpp:51`) — the server sets
  `SO_RCVTIMEO/SO_SNDTIMEO = 5s` on accepted sockets and bounds its
  main-thread dispatch wait the same way. **Unproven lead**: a terminal
  long-poll or large snapshot response that legitimately needs >5s on the
  POSIX path would die exactly like this while Windows' overlapped path
  behaves differently. Verify before trusting.
- Toast spam is a secondary effect: the announce latches
  (`app.cpp` topology/agent poll announcements) reset on every transient
  recovery, so a flapping channel re-toasts per flap cycle.

## Failing unit coverage (draxul-test-core, this Mac: 33 cases / 954)

- `tests/server_kernel_tests.cpp` — ~23 failing sites (attach/lease/token/
  spoof/resize/scrollback areas; e.g. :1047, :1185, :1328, :1360, :1603,
  :2071, :2514, :2884, :3168, :4125, :4410, :647).
- `tests/remote_terminal_host_tests.cpp` — 9 failing sites (:266, :464,
  :675, :950, :1071, :1178, :1250, :1365, :1764).
- These reproduce in-process with short /tmp paths, so they are NOT the
  sun_path issue; they are the deterministic window into the same subsystem
  and should be fixed first — the GUI storm is likely the same defect(s).
- Bisected: 4 of these pre-date the endpoint-key change (5 before it, 4
  after — the change fixed one, introduced none).
- Not this card: `config_parity_tests.cpp:221` / `config_schema_tests.cpp:226`
  correlate with in-flight local markdown/config edits (goldens);
  `agent_discovery_tests.cpp:8` and `rpc_integration_tests.cpp:81` are
  unrelated one-offs worth a look while in there.

## Implementation

- [ ] Reproduce one failing `server_kernel` remote-terminal test in isolation
      and diagnose the POSIX-side failure (timeout semantics, partial
      frame I/O, non-blocking edge, or message-size limits).
- [ ] Decide the long-request story on POSIX: per-request wire deadlines are
      already threaded through the client (`client_read_frame(fd, bytes,
      deadline)`) — the server side's fixed 5s socket timeouts and dispatch
      wait must honor the request's `timeout_ms` instead of `kIoTimeout`.
- [ ] Re-run the GUI end-to-end on macOS: shared shell appears, no
      interruption warnings in a 60s idle session.
- [ ] Coalesce the recovery toasts: announce once per outage, not per flap
      (rate-limit the latch reset, or only re-announce after N stable
      seconds).

## Acceptance

- [ ] `draxul-test-core` remote-terminal and server-kernel suites green on
      macOS.
- [ ] Clean plain launch on macOS: one server, working shared terminal, zero
      interruption warnings at idle.
- [ ] Windows behaviour unchanged (CI).

## Dependencies and parallelism

Builds on `92c00687` (macOS server startup fix). Independent of the pending
00-08 CMake sequence. One owner should hold `libs/draxul-control` transport
and the terminal runtime/service pair together — the failure sits in their
interaction.
