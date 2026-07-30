# Restore existing saved Sessions under the shared server

**Type:** bug
**Priority:** P1 / sequence 22
**Raised by:** Claude (server/client runtime review, 2026-07-30)
**Evidence:** [plans/reviews/2026-07-30-server-client-runtime-review.md](../../plans/reviews/2026-07-30-server-client-runtime-review.md#persistence)

## Goal

Making the shared server the default must not silently orphan every Session the user already
saved. Today it does, and the session CLI then operates on a store the default runtime never
reads.

## Observed behaviour

**Two stores, no migration.** The server reads and writes only `<runtime_dir>/sessions/`
(`server_session_state_path`, `libs/draxul-server/src/server_kernel.cpp:148-162`, and the
restore scan at `:572-611`). It never reads the legacy `<config>/sessions/` store. A user who
upgrades and launches normally sees a single fresh shell instead of their saved layout, with
no message.

The plan explicitly calls for this: "The first server migration should read the current
snapshot path and schema without rewriting it at startup"
(`plans/server-client-terminal-runtime.md`, Persistence and restore). It is not implemented.

**The CLI straddles both stores.** `--list-sessions` now queries the running server and exits
1 with "Draxul server is unavailable" when there is none (`app/main.cpp:288-318`), while
`--rename-session` and `--delete-session` still operate on the legacy files via
`delete_session_state`/`rename_saved_session_record` in `app/session_cli.cpp`. So
`draxul --delete-session --session X` reports success against a file the default runtime never
reads, and Session X survives on the server.

> **In flight as of 2026-07-30.** The working tree (uncommitted at the time this card was
> written) already routes `--delete-session` through a new `server.delete_session` kernel
> method and `ServerClient::delete_session`, removing the legacy delete path from
> `app/session_cli.cpp`. That covers the delete half of the CLI split-brain below — confirm it
> landed before starting, and do not redo it. `--rename-session` still uses
> `rename_saved_session_record` against the legacy store, and the migration itself (the larger
> half of this card) is untouched.

## Decide the policy first

- [ ] One-time import (copy legacy Sessions into the server store on first run, leaving the
      originals) versus read-through fallback (server reads the legacy path when its own is
      absent). Proposal: read-only import on first run, matching the plan's "without rewriting
      it at startup" — it keeps `--no-server` working against the untouched originals during
      the confidence period.
- [ ] Whether `--rename-session`/`--delete-session` should route through the server, operate on
      both stores, or refuse with a message naming the store. Proposal: route through the
      server when one is running, matching `--list-sessions`.

## Implementation

- [ ] Seed the server's Session store from `<config>/sessions/` on first run, without
      modifying the legacy files.
- [ ] Route `--rename-session` through the server when one is available, so all four verbs
      address the same store. (`--delete-session` is already in flight — see the note above.)
- [ ] Make each session CLI verb state which store it acted on in its output and in `--help`,
      for as long as both exist.
- [ ] Log and toast once on first run when Sessions are imported, so the user knows why their
      layout did or did not come back.
- [ ] Record the eventual retirement of the legacy store in
      `plans/server-client-terminal-runtime.md` — this card creates a migration, not a
      permanent second home.

## Unit tests

- [ ] A populated legacy store with no server store produces the same restored topology after
      first server start.
- [ ] Import runs once: a second start does not re-import or duplicate Sessions.
- [ ] Import does not modify or delete the legacy files.
- [ ] `--delete-session` against a running server removes the Session the server actually
      serves.
- [ ] A legacy file that fails to parse is reported rather than silently skipped (coordinate
      with `kanban/pending/21`).

## Acceptance criteria

- [ ] Upgrading to the shared-server default restores the user's existing saved Sessions.
- [ ] All session CLI verbs address one store, or say which one they addressed.
- [ ] `docs/features.md` describes where Sessions live and what the CLI verbs act on.
- [ ] Full build, `ctest`, and smoke pass on both platforms.

## Dependencies and ownership

Shares `libs/draxul-session-model` and the server restore path with
`kanban/pending/21 checkpoint-durability -bug.md`; one owner should hold both, since the import
must not trip the corrupt-checkpoint latch that card removes. Should not trail the default
switch — this is user-visible data loss on upgrade.
