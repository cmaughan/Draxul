# Make session-persistence outcomes observable and consistent

**Type:** bug
**Priority:** P2 / sequence 10
**Raised by:** Claude (architecture review, 2026-07-27)

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
   `kanban/pending/09 multi-instance-session-endpoint-collisions -bug.md`).

## Decide the policy first

- [ ] Confirm whether `exit` in the last shell *should* delete the saved
      session, or whether it should save like a window close. Record the
      decision; do not silently change long-standing behaviour.
- [ ] Decide whether a non-shell pane should skip only that pane, or continue
      disabling the whole snapshot as today.
- [ ] Decide whether a failed `--new-session` should abort rather than
      downgrade to `default`.

## Implementation

- [ ] Surface a one-time, non-spammy signal when persistence is disabled by the
      shell-only gate (status text, toast, or a single INFO on transition) —
      report the transition, not every frame.
- [ ] Make the last-pane-exit and window-close paths agree with the recorded
      decision, and document the chosen semantics in `docs/features.md`.
- [ ] Either enforce a maximum checkpoint staleness using
      `last_session_checkpoint_time_`, or delete the dead field and document the
      debounce as intentional.
- [ ] Make `--new-session` failure explicit rather than a silent downgrade.
- [ ] Disambiguate the `--session` collision: `parse_control_cli`
      (`app/control_cli.cpp`) treats `--session <id>` as "address this running
      instance", while the app treats it as "restore this session", separated
      only by the presence of a control subcommand. At minimum document it; a
      distinct flag name for the control CLI is preferable.

## Unit tests

- [ ] Adding a non-shell pane stops checkpointing **and** emits exactly one
      transition signal; removing it resumes persistence.
- [ ] Window close vs last-shell `exit` produce the agreed, identical-by-policy
      outcome; assert the saved file's presence/absence explicitly.
- [ ] A continuously mutating session still checkpoints within the staleness
      ceiling (if one is adopted).
- [ ] `--new-session` with a colliding explicit id fails or downgrades per the
      recorded decision, with the signal asserted.

## Acceptance criteria

- [ ] No path discards or skips a saved session without a user-visible or
      logged reason.
- [ ] Window close and last-shell exit are consistent with one documented rule.
- [ ] `docs/features.md` describes the persistence rules a user can actually
      predict.
- [ ] Full build, `ctest`, and smoke pass on both platforms.

## Dependencies and ownership

Independent of the pending 00-08 sequence. Shares the session subsystem with
`kanban/pending/09 multi-instance-session-endpoint-collisions -bug.md`; if both
are scheduled together, one owner should hold `app/session_state.*` and the
`App` shutdown/checkpoint paths.
