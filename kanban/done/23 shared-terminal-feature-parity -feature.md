# Close the shared-terminal parity gaps that ship as the default

**Type:** feature
**Priority:** P1 / sequence 23
**Status:** Completed 2026-07-30
**Raised by:** Claude (server/client runtime review, 2026-07-30)
**Evidence:** [plans/reviews/2026-07-30-server-client-runtime-review.md](../../plans/reviews/2026-07-30-server-client-runtime-review.md#regressions-from-making-the-shared-server-the-default)

## Goal

`should_use_shared_server` makes every default shell a `RemoteTerminalHost`, so any behaviour
`LocalTerminalHost` has and it lacks is a regression the day the default flips. These are the
gaps a user notices in the first session.

`kanban/pending/11 remote-terminal-parity-scrollback -feature.md` tracks scrollback and
selection parity for the experimental path; this card is the default-mode regression list and
should be read alongside it.

## Observed behaviour

- **Copy mode is missing entirely.** 60 references to `copy_mode` across
  `libs/draxul-host/src/local_terminal_host.cpp` and its header; **zero** in
  `remote_terminal_host.*`. `GuiActionHandler::toggle_copy_mode` calls
  `host->dispatch_action("toggle_copy_mode")`, which `RemoteTerminalHost::dispatch_action`
  does not handle, so the documented keybinding (`docs/features.md:198`) silently no-ops.
- **Keyboard scrollback keys go to the shell.** `LocalTerminalHost` handles Shift+PageUp/
  PageDown/Home/End locally (`local_terminal_host.cpp:336-356`); `RemoteTerminalHost::on_key`
  encodes and sends them to the remote process instead (`remote_terminal_host.cpp:751-768`).
- **Tab auto-naming is dead.** Projected tabs force `local_tab->name_user_set = true`
  (`app/app.cpp:3459-3460`), which `refresh_tab_default_names` skips, and the remote `add_tab`
  path creates tabs literally named `"Tab"`. In local mode tabs rename to the focused pane's
  cwd basename, so shared mode shows "Tab / Tab / Tab" forever.
- **Save/Load Session palette actions are dead.** `enable_session_restore = allow_session_restore
  && !shared_server` (`app/main.cpp:750-753`), so both actions return "Session restore is not
  enabled for this launch" (`app/app.cpp:4448-4452`, `:4515-4519`) — naming a flag the user
  never set, while the server supports named Sessions only via `--session <id>` at launch.
- **Observer keystrokes vanish silently.** `on_key` swallows input when not the controller
  (`remote_terminal_host.cpp:751-754`) with no feedback and no take-control hint.
- **Resize while scrolled back shows the wrong thing.** `compose_scrollback_view` silently
  returns the live snapshot when `page.snapshot->cols != live.cols` (`:82-87`) while
  `scroll_offset_` stays > 0, so the status bar still reports `[n/total]` over live content
  (`:1075-1078`). The local host scrolls to live on resize.
- **Title is set every published state** (up to 40 Hz) with no changed-guard (`:671-672`),
  unlike the nvim host which only sets it on title events.

## Implementation

- [x] Implement copy mode over the projected snapshot plus scrollback pages. All of it is
      client-local state per the design's per-client list, so no protocol change is needed.
- [x] Handle scrollback keys locally in `RemoteTerminalHost::on_key` before encoding for the
      remote shell.
- [x] Carry a `name_user_set` bit in `TopologyTab` (the server knows whether `RenameTab` ever
      ran) so clients can auto-name non-user-set tabs locally.
- [x] Route Save/Load Session to server named Sessions, or hide the actions in shared mode and
      correct `docs/features.md:244`.
- [x] Give observer input a hint or toast pointing at take-control.
- [x] Reset `scroll_offset_` when a scrollback page's columns no longer match live.
- [x] Guard `set_window_title` on an actual change.
- [x] Audit `docs/features.md` for anything else documented as "shell hosts" that the remote
      host does not implement, and either implement or annotate it.

## Unit tests

- [x] Copy mode enters, moves, selects, and yanks against a projected snapshot and a
      scrollback page.
- [x] Shift+PageUp scrolls the remote pane rather than reaching the shell.
- [x] A projected tab with no explicit rename auto-names from cwd.
- [x] Resizing while scrolled back leaves offset and status bar consistent.
- [x] Title is published once per change, not per frame.

## Acceptance criteria

- [x] Every capability `docs/features.md` attributes to shell hosts works on a default
      (server-owned) shell, or the document says precisely where it does not.
- [x] A user switching from `--no-server` to the default sees the same documented terminal
      interaction; the intentional Session-palette boundary is documented.
- [x] Windows Release/Ninja core and app test targets build and focused parity suites pass;
      macOS and renderer snapshots remain covered by CI.

## Validation

- `[host][remote-terminal]`: 1,229 assertions in 11 cases.
- Copy mode, selection Ctrl+C, Shift navigation, observer hints, resize mismatch recovery,
  title de-duplication, large-paste ordering, and request-error recovery pass.
- Protocol/server tests cover default/user-set tab naming and legacy payload fallback;
  palette tests cover shared-mode Save/Load visibility.
- macOS/POSIX and backend render snapshots remain CI gates.

## Dependencies and ownership

Read with `kanban/pending/11 remote-terminal-parity-scrollback -feature.md`. Strongly related
to `kanban/pending/27 topology-projection-extraction -refactor.md`: these gaps exist because
`App` now has two mutation paths, so `27` is the structural fix that stops this class of
regression recurring. Related ice-box: `20 searchable-scrollback -feature.md`.
