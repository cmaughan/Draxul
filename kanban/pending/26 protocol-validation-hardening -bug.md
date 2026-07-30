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

- [ ] One range-checking extraction helper (`int64_t`/`uint64_t` then bounds-check) applied to
      every narrowing `get<>` in `draxul-protocol`.
- [ ] Reject duplicate coordinates in full deltas (seen-bitmap) and bound non-full delta cell
      counts by `cols * rows`.
- [ ] Add `is_number_unsigned()` checks to status counters; reuse the welcome string caps and
      the named `kServerMaxSessions` constant.
- [ ] Apply the `session_id` control-character filter to `client_id` and
      `controller_client_id`.
- [ ] Validate cursor and shell-mark coordinates against grid bounds at parse.
- [ ] Reconcile the scrollback page row bound with the codec — either lower the constant to 200
      or give pages their own bound.
- [ ] Issue a server-assigned connection token at `server.hello` and require it alongside
      `client_id` for lease-affecting methods.
- [ ] Add `PIPE_REJECT_REMOTE_CLIENTS` to both pipe creates, keep a reserved
      `FIRST_PIPE_INSTANCE` handle for the process lifetime so instances never drop to zero,
      and pass `SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION` on the client open.
- [ ] Write metadata via temp file + `fchmod`/explicit DACL + rename, with `O_NOFOLLOW`; apply
      an explicit DACL to the runtime directory on Windows (POSIX already chmods 0700).
- [ ] Skip unknown event kinds and default absent command fields, so additive evolution is
      possible within a minor version; count skips in metrics.
- [ ] Delete the dead expanded cell parse path, and either use or drop `server_sequence`.

## Unit tests

- [ ] A `protocol_major` of `2^32 + 1` is rejected, not truncated to a compatible value.
- [ ] A full delta with duplicate coordinates is rejected.
- [ ] Negative status counters, oversized status strings, and control characters in
      `client_id` are rejected.
- [ ] A scrollback request at the advertised maximum round-trips through the client codec.
- [ ] An unknown event kind is skipped rather than failing the poll.
- [ ] Metadata written over a pre-existing file has owner-only permissions.

## Acceptance criteria

- [ ] No peer-supplied integer reaches a narrow type without a range check.
- [ ] A malformed peer response cannot corrupt client state or wedge a pane.
- [ ] Additive protocol changes are possible without a major version bump.
- [ ] Full build, `ctest`, and smoke pass on both platforms.

## Dependencies and ownership

Blocks Slice 10 (SSH bridge) in `plans/server-client-terminal-runtime.md`; the client parser
becomes a security boundary at that point. The malformed-event handling interacts with
`kanban/pending/19 client-recovery-state-machine -refactor.md` (reattach rather than die on
`invalid_event`) — land `19` first so this card's rejections have a recovery path. The Windows
pipe and metadata changes touch the same code as
`kanban/pending/16 posix-server-singleton-race -bug.md`.
