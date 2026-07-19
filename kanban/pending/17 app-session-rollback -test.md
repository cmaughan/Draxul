# App session rollback fault coverage

**Type:** test
**Priority:** 17
**Raised by:** Claude

## Gap

`App::load_session` and `save_session_as` coordinate workspace state, attach-server identity, metadata, and rollback lambdas. The mid-operation failure paths are not directly pinned.

## Implementation plan

- [ ] Enumerate mutation stages and rollback obligations for load, save-as, rename, attach-server restart, and restore failure.
- [ ] Extend `AppDeps`/session seams only as needed to inject state-store and attach-server failures deterministically.
- [ ] Build fixtures with multiple workspaces/panes, custom names, focus, active session id, and persisted metadata.
- [ ] Fail each stage after the preceding mutation and assert the complete pre-operation state is restored.
- [ ] Assert input host pointers, active workspace id, render tree, and frame requests are valid after rollback.
- [ ] Add success-path assertions alongside failures so the seam cannot test a different flow from production.

## Verification

- [ ] Run the focused tests under ASan where available.
- [ ] Repeat failure cases to detect leaked attach threads, files, or hosts.
- [ ] Run all session/App tests and smoke.

## Acceptance criteria

- [ ] Every rollback stage has named coverage.
- [ ] A failed operation leaves the original live/persisted session usable.
- [ ] No rollback callback captures destroyed or already-moved state.

## Dependencies and parallelism

Safety net before item 22. Coordinate with item 02 if atomic-store injection changes the test seams.

<model>GPT-5 Codex</model>

## Investigation 2026-07-19 (groundwork; not yet implemented)

Root analysis done; implementation deferred to a fresh-context pass (the harness is large and
the card warns the seam must not test a different flow from production).

**Functions confirmed present** in `app/app.cpp` (card names are NOT stale):
- `App::load_session` (2365), `App::save_session_as` (2520), `App::rename_session` (2353),
  `App::snapshot_session_state` (2644), `App::persist_session_state` (2673),
  `App::restore_session_state` (2756).

**Rollback structure** (save_session_as, 2542-2589): captures old id/name/timestamps, defines
`delete_new_files` + `rollback` lambdas (restore old state, delete new files, restart attach
server), then mutates and calls `save_session_state` / `start_session_attach_server`; on failure
it invokes `rollback`.

**Fault-injection findings:**
- State-store failure is injectable WITHOUT a code seam: `save_session_state`/`delete_session_state`
  (session_state.h) are free functions writing under the config dir, so a `HomeDirRedirect` to an
  unwritable path makes them fail deterministically (mirror session_state_tests.cpp's redirect).
- Attach-server restart failure (`start_session_attach_server`) still needs a small deterministic
  failure seam (no injection point today).

**The real blocker (why this is deferred):** no fixture drives a *live* App through save/load. To
call `save_session_as` you need `can_snapshot_session_state()` true, i.e. a fully-initialized App
with restorable shell panes. `startup_rollback_tests.cpp` only reaches init-failure via the AppDeps
window/renderer/host factories; `session_state_tests.cpp` only exercises the free store functions.
Neither builds a running App with a snapshot-capable host + workspaces.

**Next-pass plan:** (1) build an App-with-restorable-session fixture (AppDeps fakes + a fake host
whose `can_snapshot_session_state()` returns true and yields a deterministic AppSessionState);
(2) inject state-store failure via an unwritable HomeDirRedirect; add a tiny attach-server-start
failure hook; (3) fail each stage after its preceding mutation and assert old id/name/timestamps,
active workspace id, input host pointers, render tree, and frame requests are all restored;
(4) add success-path assertions so the seam can't diverge from production. Card stays in pending.
