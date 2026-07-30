# Make server checkpoints durable and recoverable

**Type:** bug
**Priority:** P1 / sequence 21
**Raised by:** Claude (server/client runtime review, 2026-07-30)
**Evidence:** [plans/reviews/2026-07-30-server-client-runtime-review.md](../../plans/reviews/2026-07-30-server-client-runtime-review.md#persistence)

## Goal

One bad shutdown must not silently disable persistence forever. Today a corrupt checkpoint
permanently stops all future writes for that Session, and the user is never told in the window
they actually use.

## Observed behaviour

**No fsync before rename.** `save_session_state_to_path` writes a `.tmp` then renames
(`libs/draxul-session-model/src/session_state.cpp:1148-1197`), but never fsyncs the temp file:
POSIX does `out.flush()` then `std::filesystem::rename`, and Windows uses
`MOVEFILE_WRITE_THROUGH` on the *move* rather than `FlushFileBuffers` on the data. Power loss
can leave a truncated or empty renamed file. The temp+rename pattern is right; the durability
half is missing.

**Corruption is a permanent, silent kill switch.** On restore failure `prepare_session_restore`
sets `prepared->checkpoint_enabled = false; checkpoint_state = "disabled"`
(`libs/draxul-server/src/server_kernel.cpp:510-519`), and `checkpoint_session` then fails with
"Checkpoint disabled to preserve the previous Session file after restore warnings"
(`:953-957`). `server_kernel.cpp:312` — a struct initializer — is the **only** assignment of
`true` anywhere in the codebase, so nothing ever re-enables it. The same latch also fires for a
merely *partial* restore (`:538-544`), so a single non-fatal warning disables persistence for
the life of that Session file.

**The user is never told.** `restore_warnings` and `checkpoint_state` are carried in the status
snapshot but `app/app.cpp` never references `restore_warnings`; only `--server-status` and the
tray surface them. So the user works for weeks and every cold restart quietly restores the
stale pre-corruption layout.

This is the same silence class as `kanban/pending/10 session-persistence-silent-loss -bug.md`,
reintroduced one layer down in the server.

## Decide the policy first

- [ ] What should happen to a checkpoint that fails to load? Preserving the file (current
      behaviour) protects a possibly-recoverable layout, but permanently disabling writes is
      the wrong second half. Proposal: rename the bad file aside as `.corrupt-<timestamp>`,
      log and toast once, and resume checkpointing to a fresh file — preserving the evidence
      without sacrificing all future saves.
- [ ] Should a *partial* restore (warnings but a usable topology) disable checkpointing at all?
      Proposal: no — warn once and keep saving.

## Implementation

- [ ] fsync the temp file (and, on POSIX, the containing directory) before rename; use
      `FlushFileBuffers` before `MoveFileExW` on Windows.
- [ ] Rename a corrupt checkpoint aside instead of latching persistence off, and re-enable
      checkpointing to a fresh file.
- [ ] Separate "restore had warnings" from "checkpointing is disabled" — they are currently
      the same flag.
- [ ] Push `restore_warnings` and a non-`ok` `checkpoint_state` to attaching clients as a
      toast, so the failure is visible where the user is.
- [ ] Move the checkpoint write off the kernel state thread, or bound it. It currently runs
      synchronously in the loop (`server_kernel.cpp:2663-2692`), so a slow disk stalls every
      client's request window — the plan specifies persistence workers.

## Unit tests

- [ ] A truncated or garbage checkpoint file is renamed aside, the Session starts fresh, and
      the next checkpoint succeeds.
- [ ] A partial restore with warnings still checkpoints afterwards.
- [ ] `checkpoint_state` and `restore_warnings` reach an attached client.
- [ ] Interrupting a write leaves either the previous good file or the new one, never a
      truncated file (as far as the harness can simulate).

## Acceptance criteria

- [ ] No single bad shutdown can permanently disable persistence for a Session.
- [ ] A persistence failure is visible in the UI, not only in `--server-status`.
- [ ] A slow disk does not stall client requests.
- [ ] Full build, `ctest`, and smoke pass on both platforms.

## Dependencies and ownership

Shares the observability goal with `kanban/pending/10
session-persistence-silent-loss -bug.md` and the durability goal with
`kanban/ice-box/02 atomic-session-persistence -bug.md` — consider pulling that ice-box card
into this one, since `save_session_state_to_path` is now shared by both the app and the
server. One owner should hold `libs/draxul-session-model` and the server checkpoint path.
