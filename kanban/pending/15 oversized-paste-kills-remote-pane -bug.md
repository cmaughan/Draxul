# Stop a large paste from permanently killing a remote pane

**Type:** bug
**Priority:** P0 / sequence 15
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

- [ ] Chunk paste and input at a safe size below the server limit in `send_paste`/`enqueue`,
      preserving bracketed-paste framing across chunks.
- [ ] Fix the batching guard to compare against the limit directly rather than subtracting
      (`back.size() >= limit` means start a new command).
- [ ] Reclassify `invalid_input` — and the backpressure code introduced by
      `kanban/pending/13` — as expected: toast and drop, never fatal.
- [ ] Give a fatal worker exit a recovery path. A remote host that dies must either be
      recreated by the app or present an explicit "reconnect" affordance; a live server
      terminal must never be orphaned behind a dead pane. Coordinate with
      `kanban/pending/19 client-recovery-state-machine -refactor.md`, which owns the general
      policy.
- [ ] Surface `enqueue` failure to the user instead of dropping silently.

## Unit tests

- [ ] A paste larger than the server limit reaches the shell intact via chunking.
- [ ] A rejected input command toasts and leaves the worker running and the pane usable.
- [ ] Enqueueing an oversized command followed by keystrokes does not merge them (regression
      test for the underflow).
- [ ] A fatal worker exit results in a recoverable pane, not a permanent ghost.

## Acceptance criteria

- [ ] Pasting a multi-hundred-KB buffer into a shared terminal works.
- [ ] No single rejected command can permanently freeze a pane whose shell is alive.
- [ ] Dropped input is always visible to the user.
- [ ] Full build, `ctest`, and smoke pass on both platforms.

## Dependencies and ownership

Depends on `kanban/pending/13` for the backpressure error code. The permanent-ghost half
overlaps `kanban/pending/19`; if both are scheduled together, one owner should hold
`remote_terminal_host.cpp`. Related existing card:
`kanban/ice-box/34 large-paste-stress -test.md`.
