# Stop invalid UTF-8 shell output from terminating the server

**Type:** bug
**Priority:** P0 / sequence 12
**Raised by:** Claude (server/client runtime review, 2026-07-30)
**Evidence:** [plans/reviews/2026-07-30-server-client-runtime-review.md](../../plans/reviews/2026-07-30-server-client-runtime-review.md#1-invalid-utf-8-in-shell-output-terminates-the-server)

## Goal

A server-owned shell that prints a non-UTF-8 byte must not kill the server process. Today it
kills every shell in every Session, with no client involvement and no hostile actor.

## Observed behaviour

Verified end to end on `2830ce17`:

1. `utf8_sequence_length` returns 1 for lead bytes `0x80`-`0xC1` and `0xF5`-`0xFF`
   (`libs/draxul-types/include/draxul/unicode.h:32-43`), so `has_complete_codepoint` accepts
   them and `consume_cluster` returns the **raw byte**, not U+FFFD
   (`libs/draxul-terminal-core/src/vt_parser.cpp:19`, `:60`).
2. `write_cluster` stores it unchanged (`libs/draxul-terminal-core/src/terminal_core.cpp:578`)
   and `capture_cell` copies it verbatim into the snapshot
   (`libs/draxul-terminal-core/src/terminal_snapshot.cpp:96`).
3. `make_delta_event` calls `.dump()` on the encoded event
   (`libs/draxul-server/src/remote_terminal_service.cpp:157`). nlohmann's default
   `error_handler_t::strict` throws `type_error.316` on invalid UTF-8; the repo sets no custom
   handler anywhere.
4. That call runs on the kernel loop thread **outside** `process_pending`'s try/catch
   (`libs/draxul-server/src/server_kernel.cpp:2578`), and the `std::jthread` running the loop
   has no handler (`app/main.cpp:256`), so it reaches `std::terminate`.

Reproduce with `cat` on any binary, `type foo.exe`, or Latin-1 output. It fires with **zero
clients attached**, because `make_delta_event` runs whenever `pump()` sees output — and the
`.dump()` at `:157` exists only to increment the `delta_bytes_` metrics counter. The same
throw is reachable on listener threads from the response `dump()` at
`libs/draxul-control/src/control_plane.cpp:812`.

The local terminal path never JSON-encodes cells, so this is new to this branch.

## Decide the policy first

- [x] Sanitize at cell write (grid never holds invalid UTF-8, matching what a terminal
      actually displays) or at snapshot capture (grid keeps raw bytes, wire stays clean).
      Cell-write is preferred: it makes the invariant hold for every consumer, including
      future ones, and matches the U+FFFD that `utf8_decode_next` already computes and
      discards.

## Implementation

- [x] Emit U+FFFD for invalid sequences in `consume_cluster` rather than returning the raw
      bytes — `utf8_decode_next` already produces the replacement codepoint at
      `unicode.h:124-126` and the result is thrown away.
- [x] Belt and braces on the wire: use
      `dump(-1, ' ', false, nlohmann::detail::error_handler_t::replace)` for every JSON
      carrying terminal content, including the control-plane response at
      `control_plane.cpp:812`.
- [x] Delete the metrics-only `.dump()` calls in `make_delta_event` and `snapshot_event`
      (`remote_terminal_service.cpp:143`, `:157`); count bytes when an event is actually
      encoded into a poll response instead. This removes a full serialize-and-discard per
      delta per tick and one of the two throw sites.
- [x] Add an exception barrier around the kernel loop body in `run_until_stopped` that logs
      and degrades one Session rather than terminating the process.

## Unit tests

- [x] Feed `0x80`, `0xFF`, a truncated multi-byte sequence, and a valid multi-byte sequence
      through `TerminalCore` and assert the captured snapshot round-trips through
      `remote_terminal_event_to_json(...).dump()` without throwing.
- [x] `ServerTerminalRuntime` pump with binary output and **no subscribers** completes and
      leaves the kernel loop running.
- [x] Existing Unicode/grapheme tests still pass — U+FFFD substitution must not disturb valid
      cluster handling or double-width accounting.

## Acceptance criteria

- [x] `cat` on a binary file in a server-owned shell leaves the server and all other shells
      running.
- [x] No JSON `dump()` on a path carrying terminal content can throw.
- [x] The kernel loop survives an exception from any single Session's pump.
- [ ] Full build, `ctest`, and smoke pass on both platforms.

## Dependencies and ownership

Independent; touches `draxul-terminal-core`, `draxul-server`, and `draxul-control`. Should
land before any other card in this wave — the review's recommended order starts here. Same
owner as `kanban/done/13 blocking-pty-io-stalls-server -bug.md`, since both harden the
kernel loop.
