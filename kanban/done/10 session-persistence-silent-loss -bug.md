# Make session-persistence outcomes observable and consistent

**Type:** bug
**Priority:** P2 / sequence 10
**Raised by:** Claude (architecture review, 2026-07-27)

## Resolution (2026-07-30)

Superseded by server-owned Sessions and durable server checkpoints. Shell
topology is no longer conditionally saved by each UI, a window disconnect does
not end the Session, and a shell process exit is reconciled by the authoritative
server topology. `--new-session` is also resolved against the live server
registry and fails instead of falling back to another Session.

The historical client-side observations and fixes below are retained for
context, but their app-owned persistence path, `SessionCli`, and `--no-server`
route have now been removed.

## Goal

Four separate paths currently discard, skip, or redirect a user's saved session
with no user-visible signal. Each individual policy appears deliberate — the
code comments say so — but together they are inconsistent and silent, so a user
loses a layout without ever learning why.

Scope is observability plus reconciling the inconsistency. This card does not
propose making non-shell hosts restorable (still explicitly out of scope per
`docs/features.md:141`).

## Observed behaviour

1. **One non-shell pane disables all persistence.**
   `PaneManager::has_restorable_shell_session` (`app/pane_manager.cpp:546-560`)
   returns false if *any* pane's launch kind fails `is_terminal_shell_host`.
   `App::can_snapshot_session_state` gates both checkpointing
   (`app/app.cpp:2946-2949`) and the shutdown save, so a single nvim, markdown,
   kanban, score, or megacity pane anywhere silently stops every write. The
   user's shell layout stops being saved with no indication.

2. **Closing the window saves; exiting the last shell erases.**
   `App::shutdown` saves when `!discard_session_state_on_shutdown_`
   (`app/app.cpp:3545-3546`), but two paths set that flag *and* call
   `delete_session_state` first: closing the last pane of the last tab
   (`app/app.cpp:1002-1004`) and the final host exiting on its own
   (`app/app.cpp:1801-1803`). Both are commented as intentional. The result is
   that titlebar-close preserves the topology while typing `exit` destroys it —
   an asymmetry no user would predict.

3. **Checkpointing has no maximum staleness.** `maybe_checkpoint_session`
   (`app/app.cpp:2946-2957`) compares against `last_session_mutation_time_`, so
   it is a trailing-edge debounce: a session mutated more often than
   `session_checkpoint_interval` (default 2 s) never checkpoints while busy.
   `last_session_checkpoint_time_` is assigned at `app.cpp:2821`, `:2891`, and
   `:2956` but **never read in any condition** — the field that would enforce a
   staleness ceiling is dead.

4. **`--new-session` can silently land in the shared default session.**
   When `prepare_new_session_launch` fails (for example, an explicit
   `--session` id that already exists), `main.cpp:173-181` logs a WARN and sets
   `session_id = "default"`, `new_session = false`, because "the window must
   appear". The user asked for an isolated session and silently got the shared
   one — which is also the session most likely to collide (see
   `kanban/done/09 multi-instance-session-endpoint-collisions -bug.md`).

## Decide the policy first — decided 2026-07-27

- [x] `exit` in the last shell **keeps deleting** the saved session: ending the
      final console means the work is finished, not parked. The asymmetry with
      a window close (which saves) is intentional and is now documented rather
      than changed.
- [x] The shell-only gate **keeps its current behaviour** — one non-shell pane
      still disables the whole snapshot. Only its silence is fixed.
- [x] A failed `--new-session` **aborts**. Downgrading to `default` handed back
      the shared Session the user was avoiding, and `default` is now the id
      most likely to be refused for already being open.

## Implementation

- [x] Surface a one-time signal when persistence is disabled by the shell-only
      gate: `maybe_checkpoint_session` latches the state and logs plus toasts
      on the transition only — never the steady state, and never on the first
      evaluation, so an explicit non-shell launch does not open with a warning.
      It also reports when persistence resumes.
- [x] Document the exit-vs-close semantics in `docs/features.md` (behaviour
      unchanged, per the decision above).
- [x] Deleted the dead `last_session_checkpoint_time_` field (assigned in three
      places, never read) and documented the trailing-edge debounce as
      intentional. No staleness ceiling: mutations are discrete user actions,
      so a session that never settles for 2s is not a shape we have seen.
- [x] `--new-session` failure now prints the error and exits 1.
- [x] Documented the `--session` collision: with a control subcommand it
      addresses a running instance; without one it selects a saved session.
      Left the flag name alone — renaming it is a breaking CLI change that
      should not ride along with a bug fix.

## Unit tests

- [x] The gate condition itself is covered: a product-host pane makes
      `has_restorable_shell_session()` false, a shell pane makes it true
      (`tests/pane_manager_tests.cpp:686-690`).
- [x] Staleness ceiling: not applicable — none adopted (see above).
- [x] Superseded: the client-side shell-only transition latch is disabled in the
      production shared-server path. Server checkpoint/restore warnings are
      published through Session status, shown once by `App`, and printed by
      `--server-status`; checkpoint failure/recovery coverage lives in
      `server_session_checkpoint_tests.cpp`.
- [x] Superseded: `SessionCli` and its fallback path were removed. `--new-session`
      now resolves against the live server registry and exits with the returned
      error when an explicit id exists. Named-Session isolation, creation, and
      duplicate handling are covered by the server Session/topology suites; the
      CLI contract is documented in `docs/features.md`.

## Acceptance criteria

- [x] Server checkpoint/restore failures remain in per-Session status and are
      visible in an attaching UI and `--server-status`; corrupt inputs are
      archived and partial restores remain writable.
- [x] Closing all windows leaves the server-owned Session and processes alive;
      clean final-shell exit closes attached UIs while the server remains
      running. Both rules are documented and covered by server integration tests.
- [x] `docs/features.md` describes server-owned checkpoints, warning delivery,
      client detach, clean process exit, and the no-fallback `--new-session`
      behavior.
- [x] macOS full build, `ctest`, and smoke pass.
- [x] Windows full build, `ctest`, and smoke pass (48/48 products aggregate and
      same-cache smoke, recorded in the card 13 Windows closeout).

## Shared-server audit (2026-09-23)

No production or test seam is missing for the behavior that ships today. The two
historic unchecked tests targeted deleted client-owned code. Their replacement
coverage is explicit:

- `tests/server_session_checkpoint_tests.cpp` verifies detached-client survival,
  periodic and shutdown checkpoints, last-good-file preservation, contained
  failures, corrupt-checkpoint archival, and writable partial restore.
- `tests/server_topology_terminal_tests.cpp` verifies that clean shell exit is
  reconciled into the shared topology and observed by every client.
- `app/app.cpp` consumes published `persistence_warnings` as one-time Session
  persistence toasts; `app/main.cpp` prints checkpoint errors and restore warnings
  in server status.
- `docs/features.md` records the user-visible persistence and Session CLI rules,
  including that an existing explicit `--new-session` id is an error rather than
  a fallback to `default`.

The legacy local snapshot path remains available only to non-shared test/embed
configurations. Production shell launches set `enable_session_restore` false when
connected to the shared server, so reviving the deleted transition-latch or
`SessionCli` tests would not exercise the product path.

## Dependencies and ownership

Independent of the pending 00-08 sequence. Shares the session subsystem with
`kanban/done/09 multi-instance-session-endpoint-collisions -bug.md`; both
are scheduled together, one owner should hold `app/session_state.*` and the
`App` shutdown/checkpoint paths.
