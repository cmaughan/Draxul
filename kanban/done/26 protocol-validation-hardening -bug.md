# Harden protocol validation before the transport stops being same-user

**Type:** bug
**Priority:** P2 / sequence 26
**Raised by:** Claude (server/client runtime review, 2026-07-30)
**Evidence:** [plans/reviews/2026-07-30-server-client-runtime-review.md](../../plans/reviews/2026-07-30-server-client-runtime-review.md#protocol-validation-hardening)

## Goal

Every item here needs a hostile or buggy *peer* to reach, so none is a live bug under today's
same-user local model. They are cheap now and expensive to retrofit, and they all become
security boundaries the moment Slice 10 (SSH bridge) lands. This card is the gate on that
slice.

The server parses client input strictly — the topology tree validation at
`libs/draxul-protocol/src/topology_protocol.cpp:244-287` is exemplary. The asymmetry is that
client-side parsers are looser, and after Slice 10 the client parser is the boundary.

## Observed behaviour

**Unchecked narrowing defeats the version gate.** `hello.protocol_major =
value["protocol_major"].get<int>()` after only an `is_number_integer()` check
(`libs/draxul-protocol/src/server_protocol.cpp:161-162`); nlohmann's `get<int>()` is an
unchecked `static_cast`, so `4294967297` truncates to `1` and passes both
`server_kernel.cpp:1067` and `server_client.cpp:291`. Same pattern at
`remote_terminal_protocol.cpp:416-417` (cols/rows), `:262-264` (cursor, shape),
`topology_protocol.cpp:558` (move_delta), `:100` and `agent_protocol.cpp:146`
(`get<uint32_t>` on version fields), `agent_protocol.cpp:255` (exit_code).

**"Full" deltas are validated by count only.** `snapshot.cells.size() != cols * rows`
(`remote_terminal_protocol.cpp:536-541`) — so 64,000 copies of cell (0,0) pass. The client then
does `cells.assign(expected_cells, {})` and overwrites only the duplicated index
(`libs/draxul-client/src/remote_terminal_client.cpp:212`, `:230`), rendering a nearly blank grid
it believes is authoritative, after which non-full deltas patch a wrong baseline.

**Other gaps:**

- status counters parsed with `get<size_t>()` and no `is_number_unsigned()`
  (`server_protocol.cpp:65-67`, `:272-276`), so `-1` becomes 18446744073709551615;
- `state`, `server_epoch`, and `build_version` unbounded in status where welcome bounds them at
  128 bytes (`:216-218` vs `:308-317`); `session_statuses` capped at a literal 256 while
  `kServerMaxSessions` is 128;
- `client_id` permits control characters where `session_id` rejects them (`:165-167` vs
  `:80-83`) — and it is echoed to other clients as `controller_client_id` and into logs, so it
  is a terminal/log escape injection vector;
- cursor and shell-mark coordinates unvalidated against grid bounds
  (`remote_terminal_protocol.cpp:262-263`, `:276-278`), saved today only by a downstream clamp;
- `kRemoteTerminalMaxScrollbackPageRows = 256` while `valid_dimensions` rejects over 200 rows,
  so a request the shared constant says is legal yields a response the client's own codec
  rejects (the `> 256` check at `:766-768` is dead code);
- client identity is entirely self-asserted — `take_control`, `terminal.disconnect`, and
  `server.goodbye` act on whatever `client_id` the caller supplies, with nothing binding it to
  the connection;
- Windows named pipes omit `PIPE_REJECT_REMOTE_CLIENTS` (reachable over SMB), only the
  *initial* instance uses `FILE_FLAG_FIRST_PIPE_INSTANCE` while each recreation drops to
  zero instances, and the client omits `SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION`
  (`libs/draxul-control/src/control_plane.cpp:719-724`, `:757-762`, `:1063-1065`);
- `write_owner_only_file` does not enforce permissions on a **pre-existing** file (Windows
  `CREATE_ALWAYS` applies the DACL only on creation; POSIX `0600` is ignored for an existing
  file) and follows symlinks (`:230-288`) — and the payload is the auth token;
- strict enum and required-field parsing (`remote_terminal_protocol.cpp:372-384`,
  `topology_protocol.cpp:520-539`) makes any additive change a de facto major break, so the
  negotiated minor version cannot gate anything;
- the expanded object-per-cell parse path is dead on the wire (nothing emits it) but doubles
  the parse surface, and `server_sequence` is emitted but never read.

## Implementation

- [x] Add one range-checking extraction helper (`int64_t`/`uint64_t` then bounds-check) and
      apply it to server, topology, and agent protocol parsing.
- [x] Apply the shared range-checking helper to the remaining remote-terminal protocol
      narrowing conversions.
- [x] Reject duplicate coordinates in full deltas (seen-bitmap) and bound non-full delta cell
      counts by `cols * rows`.
- [x] Add unsigned/range checks to status counters; reuse named string caps and
      the named `kServerMaxSessions` constant.
- [x] Apply the `session_id` control-character filter to `client_id` and expose the shared
      validator for controller identities.
- [x] Apply the shared identity validator to `controller_client_id` in the remote-terminal
      protocol.
- [x] Validate cursor and shell-mark coordinates against grid bounds at parse.
- [x] Reconcile the scrollback page row bound with the codec — either lower the constant to 200
      or give pages their own bound.
- [x] Issue a server-assigned connection token at `server.hello`, bind negotiated client
      identities to it, and require it on their subsequent requests.
- [ ] Remove the same-user legacy unnegotiated-client fallback when Slice 10 introduces a
      remote transport boundary.
- [x] Add `PIPE_REJECT_REMOTE_CLIENTS` to both pipe creates, keep a reserved
      `FIRST_PIPE_INSTANCE` handle for the process lifetime so instances never drop to zero,
      and pass `SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION` on the client open.
- [x] Write metadata via temp file + `fchmod`/explicit DACL + rename, with `O_NOFOLLOW`; apply
      an explicit DACL to the runtime directory on Windows (POSIX already chmods 0700).
- [x] Default absent optional topology-command fields while strictly validating fields that
      are present.
- [x] Skip unknown event kinds so additive evolution is possible within a minor version;
      count skips in metrics.
- [x] Delete the dead expanded cell parse path, and either use or drop `server_sequence`.

## Unit tests

- [x] A `protocol_major` of `2^32 + 1` is rejected, not truncated to a compatible value.
- [x] A full delta with duplicate coordinates is rejected.
- [x] Negative status counters, oversized status strings, and control characters in
      `client_id` are rejected.
- [x] A scrollback request at the advertised maximum round-trips through the client codec.
- [x] An unknown event kind is skipped rather than failing the poll.
- [x] Metadata written over a pre-existing file has owner-only permissions.

## Acceptance criteria

- [x] No peer-supplied integer reaches a narrow type without a range check.
- [x] A malformed peer response cannot corrupt client state or wedge a pane.
- [x] Additive protocol changes are possible without a major version bump.
- [x] Windows Release/Ninja build, full `ctest`, smoke, and render snapshots pass.
- [ ] macOS full build, `ctest`, smoke, and render snapshots pass.

## Dependencies and ownership

Blocks Slice 10 (SSH bridge) in `plans/server-client-terminal-runtime.md`; the client parser
becomes a security boundary at that point. The malformed-event handling interacts with
`kanban/done/19 client-recovery-state-machine -refactor.md` (reattach rather than die on
`invalid_event`) — land `19` first so this card's rejections have a recovery path. The Windows
pipe and metadata changes touch the same code as
`kanban/done/16 posix-server-singleton-race -bug.md`.

## Remote-terminal validation note

The remote codec is compact-only as of protocol major 2: packed colour values, deduplicated
hyperlinks, bounded/range-checked coordinates, unique dirty coordinates, and metadata
coordinates constrained to the advertised grid. Scrollback pages now advertise the same
200-row maximum the snapshot codec accepts. Unknown event kinds are counted and skipped by
the client while malformed known events still enter recovery; the unused `server_sequence`
and expanded object-cell parser were removed. Windows Release/Ninja focused protocol coverage
passed 27 assertions in seven cases plus the remote-host recovery suite (1,249 assertions in
eleven cases).

Negotiated clients now receive an opaque connection token and carry it through terminal,
topology, agent, goodbye, and recovery requests. Handshake retries reuse an immutable
registration nonce so a lost welcome recovers the same token; server replacement rotates it,
and stale concurrent refreshes cannot overwrite a newer identity. The Windows integrated
gate passed all 22 CTest groups plus standalone smoke. The existing unnegotiated same-user
compatibility path is intentionally called out above as a Slice 10 removal gate, not treated
as remote authentication.
