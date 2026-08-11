# Stop a large paste from permanently killing a remote pane

**Type:** bug
**Priority:** P0 / sequence 15
**Status:** Completed 2026-07-30
**Raised by:** Claude (server/client runtime review, 2026-07-30)
**Evidence:** [plans/reviews/2026-07-30-server-client-runtime-review.md](../../plans/reviews/2026-07-30-server-client-runtime-review.md#3-a-paste-over-64-kib-permanently-kills-the-pane)

## Goal

Pasting a large buffer into a shared terminal must paste, or at worst fail with a message. It
must not leave a permanently frozen pane whose shell is still alive on the server.

## Observed behaviour

`send_paste` sends the whole clipboard as one Input command with no chunking
(`libs/draxul-host/src/remote_terminal_host.cpp:1017-1041`). The server rejects anything over
64 KiB with `invalid_input` (`libs/draxul-server/src/remote_terminal_service.cpp:353-356`).

The client's classifier treats `invalid_input` as neither expected (`not_controller`,
`invalid_resize`) nor transient (`endpoint_unavailable`, `io_error`, `main_thread_timeout`,
`server_stopping`) — see `remote_terminal_host.cpp:31-47` — so it falls through to
`log_fatal_failure(...); fatal_error = true;` at `:342-348` and the worker exits.

Recovery then does not exist. `close_dead_panes` deliberately skips server-owned panes while
awaiting a topology update that never arrives (`app/app.cpp:2085-2092`), and the app only
pumps running hosts (`app/app.cpp:2365-2367`), which also races away the final `publish_error`
toast because `running_` goes false at `remote_terminal_host.cpp:409`. The user sees a frozen
pane, the shell keeps running server-side, and only restarting the UI clears it.

Two aggravators:

- **`size_t` underflow in the batching guard.** `command.text.size() <= kRemoteInputBatchBytes
  - commands_.back().text.size()` (`remote_terminal_host.cpp:163-164`) wraps when the queued
  command already exceeds 64 KiB, so later keystrokes merge into the doomed command.
- **`enqueue`'s false return is ignored** by `send_remote_input` (`:1011-1015`), so a full
  command queue silently drops input.

## Implementation

- [x] Chunk paste and input at a safe size below the server limit in `send_paste`/`enqueue`,
      preserving bracketed-paste framing across chunks.
- [x] Fix the batching guard to compare against the limit directly rather than subtracting
      (`back.size() >= limit` means start a new command).
- [x] Reclassify `invalid_input` — and the backpressure code introduced by
      `kanban/done/13 blocking-pty-io-stalls-server -bug.md` — as expected: toast and drop,
      never fatal.
- [x] Reserve worker exit for an authoritative `terminal_not_found`. Attach and poll failures
      retry with bounded backoff; rejected or unexpected commands toast and drop while polling
      continues; scrollback failures return to live. A live server terminal is therefore never
      orphaned behind a dead pane.
- [x] Surface `enqueue` failure to the user instead of dropping silently.

## Unit tests

- [x] A paste larger than the server limit reaches the shell intact via chunking.
- [x] Rejected input commands toast and leave the worker running and the pane usable, including
      `invalid_input`, `backpressure`, `process_write_failed`, and an unknown error code.
- [x] Enqueueing an oversized command followed by keystrokes does not merge them (regression
      test for the underflow).
- [x] Malformed and unexpected poll failures reattach in place; an unexpected scrollback error
      returns to live without creating a permanent ghost.

## Acceptance criteria

- [x] Pasting a multi-hundred-KB buffer into a shared terminal works.
- [x] No single rejected command can permanently freeze a pane whose shell is alive.
- [x] Dropped input is always visible to the user.
- [x] Windows Release/Ninja core and app test targets build; the full remote-host and
      focused paste/recovery suites pass. The macOS remote-host path remains covered
      by CI.

## Validation

- `[host][remote-terminal]`: 1,229 assertions in 11 cases.
- Rejected-command recovery, ordered large paste/key input, malformed/unexpected poll
  recovery, and local copy/scroll/title parity all pass in focused Release/Ninja tests.
- macOS/POSIX runtime validation remains a CI gate.

## Dependencies and ownership

Depends on `kanban/done/13 blocking-pty-io-stalls-server -bug.md` for the backpressure error code. The permanent-ghost half
overlaps `kanban/pending/19`; if both are scheduled together, one owner should hold
`remote_terminal_host.cpp`. Related existing card:
`kanban/ice-box/34 large-paste-stress -test.md`.
