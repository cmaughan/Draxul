# Server/client runtime review — 2026-07-30

**Status:** research
**Branch reviewed:** `codex/server-client-runtime` @ `2830ce17` (committed state; the working
tree acquired uncommitted changes during the review — see "Concurrent work" below)
**Scope:** the full client/server split — wire protocol, server services and kernel,
client libraries and `RemoteTerminalHost`, IPC transport and bootstrap, and the app-layer
integration and persistence
**Related plan:** [server-client-terminal-runtime.md](../server-client-terminal-runtime.md)

This note records the findings behind `kanban/pending/12`–`27`. The cards carry the work;
this document carries the evidence, the severity reasoning, and the recommended order, so
the cards do not each have to restate it.

## Summary

The architecture is sound and the code quality is generally high. The epoch/generation/
sequence versioning, the revision-CAS topology with idempotent command ids, per-session
isolation, and the `IRemoteTerminalRuntime` seam are well designed and genuinely testable.
The defects below are implementation and policy gaps inside that architecture, not reasons
to change it.

Four defects take down or wedge real user work with no hostile actor involved, and they
should block making the shared server the default. Beyond those, the recurring themes are:
blocking IPC on threads that must not block, recovery mechanisms that depend on the thing
being recovered, and feature regressions that follow from `App` now carrying two parallel
mutation paths.

## Concurrent work

The review was performed against committed `2830ce17`. While it ran, the working tree acquired
uncommitted changes to `app/main.cpp`, `app/cli_args.cpp`, `app/session_cli.{h,cpp}`,
`libs/draxul-client/src/server_client.cpp`, `libs/draxul-server/src/server_kernel.cpp`, and
their tests. Inspected: they add a `server.delete_session` kernel method and route
`--delete-session` through it, removing the legacy delete path — which is part of the CLI
split-brain described under Persistence. That slice is noted as in-flight in
`kanban/pending/22`. Nothing else in the findings below appeared to be addressed, but the
overlap is worth re-checking before starting any card in the persistence or discovery waves.

## Severity calibration

The current trust boundary is same-user and local. Findings that require a hostile or
buggy *peer* are therefore rated as hardening rather than as live bugs — but they become
security boundaries the moment Slice 10 (SSH bridge) lands, and they are much cheaper to
fix now than to retrofit. They are collected in
`kanban/pending/26 protocol-validation-hardening -bug.md`.

## Ship-blockers

### 1. Invalid UTF-8 in shell output terminates the server

Verified end to end. `utf8_sequence_length` returns 1 for lead bytes `0x80`–`0xC1` and
`0xF5`–`0xFF` (`libs/draxul-types/include/draxul/unicode.h:32-43`), so
`has_complete_codepoint` accepts them and `consume_cluster` returns the raw byte verbatim
(`libs/draxul-terminal-core/src/vt_parser.cpp:19`, `:60`). It stays raw through
`write_cluster` (`libs/draxul-terminal-core/src/terminal_core.cpp:578`) and `capture_cell`
(`libs/draxul-terminal-core/src/terminal_snapshot.cpp:96`). `make_delta_event` then calls
`.dump()` on it (`libs/draxul-server/src/remote_terminal_service.cpp:157`), and nlohmann's
default error handler throws `type_error.316` on invalid UTF-8 — the repo sets no custom
handler anywhere.

That call runs on the kernel loop thread outside `process_pending`'s try/catch
(`libs/draxul-server/src/server_kernel.cpp:2578`), and the `std::jthread` running the loop
has no barrier (`app/main.cpp:256`), so the exception reaches `std::terminate`. Every shell
in every session dies with it.

Two aggravating details: `cat` on a binary or any Latin-1 output triggers it, and it fires
**with zero clients attached**, because that `.dump()` exists only to increment a metrics
byte counter. The local terminal path never JSON-encodes cells, so this is new to this
branch. The response-side `dump()` at `libs/draxul-control/src/control_plane.cpp:812` has
the same exposure on listener threads.

→ `kanban/pending/12 server-crash-on-invalid-utf8 -bug.md`

### 2. Blocking PTY I/O can wedge the singleton kernel thread

`terminal.input` accepts up to 64 KiB per request and runs on the kernel thread, calling
`ServerTerminalRuntime::send_input` → `process_.write()`. That is an unbounded blocking
loop on both platforms: `WriteFile` with no `OVERLAPPED` or timeout
(`libs/draxul-terminal-process/src/conpty_process.cpp:817-828`) and a `::write` retry loop
(`libs/draxul-terminal-process/src/unix_pty_process.cpp:613-627`). A child that stops
reading — Ctrl-S, a stopped foreground app, a paste flood into a busy TUI — stalls the whole
server: PTY draining stops, checkpoints stop, and every client of every session times out.

This directly violates the plan's own backpressure rule that a slow client must never block
the state loop or a PTY reader. Related: the PTY reader queues are unbounded
(`conpty_process.cpp:850-853`, `unix_pty_process.cpp:680-683`) against the same rule, so a
stall also grows server memory without limit; and `ConPtyProcess::shutdown` waits up to two
seconds per destroyed terminal on the same thread.

→ `kanban/done/13 blocking-pty-io-stalls-server -bug.md`

### 3. A paste over 64 KiB permanently kills the pane

`send_paste` sends the whole clipboard as one command with no chunking. The server rejects
anything over 64 KiB with `invalid_input`
(`libs/draxul-server/src/remote_terminal_service.cpp:353-356`). The client's classifier
treats that code as neither expected nor transient
(`libs/draxul-host/src/remote_terminal_host.cpp:31-47`), so the worker exits fatally, and
`close_dead_panes` deliberately skips server-owned panes awaiting a topology update that
will never arrive (`app/app.cpp:2085-2092`). The pane freezes permanently while the shell
keeps running server-side.

The batching guard at `remote_terminal_host.cpp:163-164` also underflows `size_t` when the
queued command already exceeds the limit, so later keystrokes merge into the oversized
command and die with it.

The deeper problem is that a fatal worker exit has no recovery path at all: `pump()` is
skipped for non-running hosts, and nothing recreates the host.

→ `kanban/done/15 oversized-paste-kills-remote-pane -bug.md`

### 4. POSIX singleton enforcement has a TOCTOU

`libs/draxul-control/src/control_plane.cpp:867-889` probes the socket for liveness, then
unlinks and binds, with no lock in between. Two UIs launching against a crashed server's
leftover socket can both probe-fail, both unlink — the second removing the first's now-*live*
socket — and both bind. Neither reports `endpoint_in_use`, so `ServerKernel::start()` returns
`Started` twice and the first server becomes an unreachable orphan still holding PTYs.

`kanban/pending/09` fixed the case where a client unlinks a live server's socket; this is the
different case of two launchers racing on a stale one. Windows has a real kernel primitive
here (`FILE_FLAG_FIRST_PIPE_INSTANCE`); POSIX needs an equivalent guarantee.

→ `kanban/pending/16 posix-server-singleton-race -bug.md`

## Blocking IPC on threads that must not block

The terminal data path got the threading right: a worker polls and the main thread only
consumes published state. Nothing else did.

Topology and agent polling run every 100 ms directly in the frame loop
(`app/app.cpp:2266-2267`), and so do topology commands, divider drags
(`flush_pending_remote_split_ratio`, one round trip per frame), status queries, and each new
pane's `attach` inside `reconcile_projected_layout`. `ControlClient::request` is fully
synchronous and re-reads the metadata file from disk on every call
(`control_plane.cpp:1023-1106`); its `kIoTimeout` of five seconds is **per I/O operation**,
not per request, so one stalled call can hold the UI thread ten to fifteen seconds. A
wedged-but-alive server does not degrade the UI, it freezes it, every 100 ms.

The plan's known-boundaries section already acknowledges bounded synchronous IPC on the UI
thread for "topology commands and projection polling", but understates both the reach (drags,
status, attach) and the duration (per-op, repeated).

Startup has the same shape: `ServerClient::ensure` blocks up to ten seconds before any window
exists (`app/main.cpp:581`, `libs/draxul-client/src/server_client.cpp:305-364`).

→ `kanban/pending/17 sync-ipc-on-ui-thread -bug.md`,
`kanban/pending/20 nonblocking-shutdown-paths -bug.md`

## Recovery mechanisms that depend on what they recover

- **`force_stop` calls `status()` first and bails when it fails**
  (`libs/draxul-client/src/server_client.cpp:439-446`) — precisely the wedged-server case it
  exists for.
- **Liveness is inferred from a bare PID** with no image name or start time
  (`server_client.cpp:51-76`). A crashed server whose PID is recycled classifies as `Busy`,
  which `ensure()` never relaunches for (`:316-318`, `:342-345`), so every launch burns the
  full timeout and fails until the metadata file is deleted by hand.
- **Stale `server-starting-*.json` markers never age out**; they are only cleaned by a
  *successful* start (`server_kernel.cpp:1048`), so a crash between marker publication and
  endpoint claim wedges launches in `Starting` the same way.
- **Protocol version mismatch is checked in three places with three outcomes** — transport
  envelope `unsupported_version` (`control_plane.cpp:199`), metadata version
  (`:305`), and `server.hello` `incompatible_protocol` — and only the third maps to
  `ServerProbeState::Incompatible`. The other two degrade into `Busy` and a timeout.

→ `kanban/pending/18 server-discovery-recovery-wedges -bug.md`

On the client, four separate and inconsistent recovery policies coexist: the host worker's
five-second grace (which expires after roughly one retry, since each failing request itself
consumes the timeout), the topology client's retry-forever, an instant-fatal path for
scrollback refresh (`remote_terminal_host.cpp:393-401`), and an epoch pinned once at process
start (`app/main.cpp:698-717`) that makes server restart unrecoverable even for newly created
panes.

Compounding this, there is no at-most-once request contract: a request that times out at the
transport is still executed by the server, because `dispatch()` abandons the wait but leaves
the `Pending` in the queue for `process_pending` to run
(`control_plane.cpp:659-697`). Meanwhile the host worker *drops* the rest of its command batch
on transient failure (`remote_terminal_host.cpp:331-341`). So a command can be silently
applied server-side while the client believes it failed, and typed input can be lost with no
feedback.

→ `kanban/pending/19 client-recovery-state-machine -refactor.md`

## Persistence

`save_session_state_to_path` writes temp-then-rename but never fsyncs the temp file
(`libs/draxul-session-model/src/session_state.cpp:1148-1197`); on Windows the
`MOVEFILE_WRITE_THROUGH` covers the move, not the data. Power loss can leave a truncated file.

When a checkpoint fails to load, the server sets `checkpoint_enabled = false`
(`libs/draxul-server/src/server_kernel.cpp:510-519`) — and `server_kernel.cpp:312` is the only
assignment of `true` anywhere, a struct initializer. Persistence for that session is disabled
permanently. The UI never surfaces `restore_warnings`; only `--server-status` and the tray do.
This is the same silence class as `kanban/pending/10`, one layer down.

Separately, the server reads and writes only `<runtime_dir>/sessions/`
(`server_kernel.cpp:148-162`) and never the legacy `<config>/sessions/` store, so existing
saved sessions silently do not restore when the server becomes the default. The plan called
for reading the current snapshot path at first migration; it is not implemented. The session
CLI straddles both stores: `--list-sessions` queries the server (`app/main.cpp:288`) while
`--rename-session` and `--delete-session` still operate on the legacy files.

→ `kanban/done/21 checkpoint-durability -bug.md`,
`kanban/done/22 legacy-session-store-migration -bug.md`

## Regressions from making the shared server the default

These are what users notice on upgrade.

- **Copy mode is absent** from `RemoteTerminalHost` — 60 references in
  `local_terminal_host.*`, zero in the remote host — so a documented keybinding
  (`docs/features.md:198`) silently no-ops in the default configuration.
- **Scrollback keys go to the remote shell.** `LocalTerminalHost` handles Shift+PageUp/Down/
  Home/End locally (`local_terminal_host.cpp:336-356`); `RemoteTerminalHost::on_key` encodes
  and sends them (`remote_terminal_host.cpp:751-768`).
- **Tab auto-naming is dead.** Projected tabs force `name_user_set = true`
  (`app/app.cpp:3459-3460`), which `refresh_tab_default_names` skips, and the remote `add_tab`
  path creates tabs literally named `"Tab"`.
- **Save/Load Session palette actions error out** naming a flag the user never set
  (`app/app.cpp:4448-4452`, `:4515-4519`, gated by `app/main.cpp:750-753`).
- **Observer keystrokes are swallowed with no feedback** and no take-control hint.
- **Resize while scrolled back** silently shows live content while the status bar still
  reports a scrollback position (`remote_terminal_host.cpp:82-87`, `:1075-1078`).
- **`set_window_title` fires on every published state** (up to 40 Hz) with no changed-guard.

`kanban/pending/11` tracks scrollback and selection parity for the experimental path; these
are default-mode regressions and are tracked separately.

→ `kanban/done/23 shared-terminal-feature-parity -feature.md`

## Topology apply-failure handling

An unknown `client_host_kind` hard-fails the client rather than degrading
(`app/app.cpp:3617-3629`). At startup this propagates out of `initialize_remote_topology` and
the window never opens; at runtime `apply_remote_topology_tabs` returns false mid-application,
leaving earlier spaces mutated, stale-tab cleanup skipped, and the input host unrestored. A
client built without `DRAXUL_ENABLE_SCOREVIEW` cannot start against a session where another
client added a score pane. The plan's own rule is to preserve unknown panes as `client_local`
rather than convert or discard; refusing entirely is the opposite failure.

Separately, `TopologyClient::poll` commits the new revision *before* the app applies it
(`libs/draxul-client/src/topology_client.cpp:65-70`), so one failed apply leaves the window
permanently out of sync — the next poll reports `changed=false` and never retries. `AgentClient`
has the same shape. Apply-failure and drag-failure toasts are also unlatched, unlike poll
failures, so a persistent failure strobes the toast stack.

→ `kanban/done/24 topology-apply-failure-handling -bug.md`

## Resource bounds

Accounting is per-object but never global. There is no cap on total terminals — topology
limits allow 128 × 256 × 256 `ServerTerminal` panes per session and neither
`create_server_terminal_with_id` (`server_kernel.cpp:379-452`) nor `TopologyService::apply`
checks a total. Each registered endpoint eagerly preallocates scrollback in its constructor
(`libs/draxul-server/src/server_terminal_runtime.cpp:36-37` →
`scrollback_buffer.cpp:112`): at the 10,000-line server default that is roughly 35 MB at 80
columns, before any process spawns or any client attaches.

Per-subscriber event queues are bounded by *count* (32) rather than bytes, while the kernel's
own comment notes that resize deltas can be several MiB apiece
(`server_kernel.cpp:42-44`). The topology idempotency cache stores 2,048 full topology
snapshots per session that are then never read, because the duplicate path overwrites the
stored snapshot with the current one (`topology_service.cpp:479-503`).

Encoding cost compounds it: every delta is serialized twice (once for real, once discarded for
the metrics counter), queued events are re-encoded on every poll attempt, colors serialize as
four-float JSON arrays, and hyperlink URIs repeat per cell rather than being deduplicated like
attrs. On the client, every published update rebuilds the whole grid
(`remote_terminal_host.cpp:625-658`), discarding the dirty-cell information the protocol
worked to deliver.

→ `kanban/pending/25 server-resource-bounds -bug.md`

## Protocol validation hardening

Reachable only from a hostile or buggy peer, so not live bugs under the current same-user
model — but they are the SSH-boundary list:

- narrowing `get<int>()` after only `is_number_integer()`, so `4294967297` truncates to `1`
  and passes the major-version check (`server_protocol.cpp:161-162`, and the same pattern in
  `remote_terminal_protocol.cpp:416-417`, `topology_protocol.cpp:558`,
  `agent_protocol.cpp:146`, `:255`);
- `full` deltas validated only by cell *count*, so duplicate coordinates pass and leave most
  of the grid blank (`remote_terminal_protocol.cpp:536-541` with
  `remote_terminal_client.cpp:212-230`);
- status counters parsed with `get<size_t>()` and no `is_number_unsigned()` check
  (`server_protocol.cpp:65-67`);
- `state`, `server_epoch`, and `build_version` unbounded in status where welcome bounds them
  at 128 bytes;
- `client_id` permits control characters where `session_id` rejects them, and it is echoed to
  other clients and into logs;
- cursor and shell-mark coordinates unvalidated against grid bounds;
- `kRemoteTerminalMaxScrollbackPageRows = 256` while the codec rejects anything over 200 rows,
  so a request the shared constant says is legal yields a response the client's own codec
  rejects;
- client identity is entirely self-asserted, so any local client can take or drop another's
  controller lease by claiming its id;
- Windows named pipes omit `PIPE_REJECT_REMOTE_CLIENTS`, only the *initial* instance uses
  `FILE_FLAG_FIRST_PIPE_INSTANCE`, and the client omits `SECURITY_SQOS_PRESENT |
  SECURITY_IDENTIFICATION`;
- strict enum and required-field parsing makes additive evolution a de facto major break, so
  the negotiated minor version cannot gate anything.

→ `kanban/pending/26 protocol-validation-hardening -bug.md`

## Structure

`app/app.cpp` is now 5,419 lines, and this branch added a second full mutation dispatch layer —
most GUI actions now fork on `if (topology_client_)`. That fork is the direct cause of the
parity regressions above: every pane feature has to be implemented twice or it diverges. The
projection state machine (id mapping tables, leaf allocation, signature diffing, command retry)
is roughly 1,200 lines with nothing renderer- or window-specific in it and belongs in
`libs/draxul-client`.

`is_remote_server_shell_kind` (`app/app.cpp:83`) and `is_server_owned_shell_kind`
(`app/cli_args.cpp:377`) are near-duplicates, and *not* identical: the `cli_args` version
enumerates every `HostKind` so the compiler catches additions, while the `app.cpp` version uses
`default:` and will silently misclassify a newly added shell kind.

The server's hand-rolled fake-terminal path duplicates the whole `RemoteTerminalService` state
machine inside the kernel (`server_kernel.cpp:273-293`, `:1978-2203`) and has already drifted
from it — generation mismatch is an error on the fake path but a snapshot resync in the
service, and the ack-pop ordering differs.

→ `kanban/pending/27 topology-projection-extraction -refactor.md`

## Recommended order

The waves are sequenced so that each one makes the next easier to validate.

1. **Ship-blockers** — `12`, `13`, `15`, `16`. Nothing else should land as default before these.
2. **UI responsiveness** — `17`, then `20`. This is what makes every other failure stop
   *feeling* like a hang, so it should come before the recovery work is judged.
3. **Recovery** — `18`, `19`. `19` subsumes the epoch pinning, the inconsistent fatal-error
   classification, and the silent input loss into one state machine; doing them separately
   would mean three partial fixes.
4. **Persistence** — `21`, `22`. `22` is user-visible data loss on upgrade and should not trail
   the default switch.
5. **Default-mode regressions** — `23`, `24`.
6. **Hardening and structure** — `25`, `26`, `27`. `26` should land before Slice 10 begins;
   `27` is the structural fix that stops `23`-class regressions recurring.

## Test gaps

The suite is genuinely good on the paths it covers — takeover, reconnect, slow-observer
resync, idempotent convergence, checkpoint failure preservation, stale identity and sequence
rejection. The gaps line up almost exactly with the findings above: nothing writes non-UTF-8
output through the remote path, sends input to a non-draining PTY, pastes over 64 KiB, races
two `ensure()` calls, exercises a recycled PID, changes the server epoch under a live client,
or holds a connection open without sending.
