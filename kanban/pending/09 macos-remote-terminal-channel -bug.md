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

## Resolution (2026-08-03)

The 5s-timeout lead above was WRONG — recorded here so it is not re-chased.
The actual root cause, found by capturing the server's reply to a failing
attach: **BSD/macOS accepted sockets inherit the listener's `O_NONBLOCK`**
(Linux's do not; Windows uses overlapped I/O). Every accepted control
connection was secretly non-blocking, so whenever the server's `recv()` ran
before the client's bytes landed, `read_frame` got EAGAIN and answered
"invalid_frame" for a valid request. Small fast requests usually won the
race; the terminal channel lost it constantly.

- [x] Diagnosed via a temporary probe at the client's id-mismatch rejection:
      the server's reply was `{"error":{"code":"invalid_frame"...},"id":""}`.
- [x] Fixed: clear `O_NONBLOCK` on accepted fds (restores the intended
      SO_RCVTIMEO semantics); `read_exact`/`write_exact` retry EINTR so
      signal delivery cannot corrupt a frame.
- [x] Suite result: 33 failing core cases → green (the one remaining failure
      is the config docs-freshness test, tied to uncommitted local config
      edits, not this subsystem).
- [x] Also swept: `executable_name()` normalizes backslashes (the neutral
      matcher parses Windows-shaped evidence on POSIX), and
      `launch_detached` double-forks so a dead detached server is reaped
      instead of lingering as a zombie that `kill(pid,0)` reports alive
      (the force-stop test failure) — argv now built pre-fork per the
      done/01 allocator-deadlock lesson.
- [x] GUI end-to-end on macOS: zero interruption warnings over 20s
      (previously 10+, escalating).
- [ ] Toast coalescing not touched: with the channel stable the per-flap
      re-announce no longer fires in normal runs; revisit only if a real
      outage shows spam again.

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
