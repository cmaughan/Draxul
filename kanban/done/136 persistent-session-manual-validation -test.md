# WI 136 — Persistent session manual validation

**Type:** test  
**Priority:** High  
**Source:** user request — 2026-04-09  
**Created:** 2026-04-09  
**Produced by:** codex

---

## Goal

Provide a practical morning test plan for the current WI 135 persistent-session
work on `develop`, covering the user-visible behaviors that now exist:

- detach on window close
- live reattach
- restore from saved topology
- explicit session create/attach/detach/rename/kill flows
- picker-based session browsing
- startup messaging that explains whether Draxul started fresh, restored, or
  reattached

This is a **manual validation** plan, not a new implementation task.

---

## Current scope

These behaviors are expected to work on the current local `develop` branch:

- Shell sessions can survive window close.
- Relaunch can reattach to a live detached session.
- If no live session owner exists, Draxul can restore saved shell topology.
- Sessions can be listed, renamed, detached, attached, and killed via CLI.
- New sessions can be created explicitly with `--new-session`.
- A session picker UI exists behind `--pick-session`.

These behaviors are **not** the current promise:

- True crash reconnect to the same still-live shell process after backend death.
- A fully headless supervisor/backend split.
- Dead-pane/remain-on-exit UX.
- Full command-palette integration for all session actions.

---

## Preconditions

- Use branch: `develop`
- Preferred launcher for manual checks:
  `r.bat run release -- ...`
- If you want a fast direct binary path after building:
  `build\Release\draxul.exe ...`
- Start from a clean mental state:
  kill any stale throwaway test sessions with `--kill-session` if needed

Useful baseline command:

```powershell
r.bat run release -- --list-sessions
```

---

## Picker controls

Inside `--pick-session`:

- `Up` / `Down` moves selection
- `Ctrl+K` / `Ctrl+J` also moves selection
- `Enter` activates the selected session
- `Delete` kills the selected session
- `F5` or `Ctrl+R` refreshes the session list
- `Esc` closes the picker
- The top `new-session` row creates a fresh session
- Typing while the picker is open turns the query into the new session name

---

## Test 1 — Fresh named session

Command:

```powershell
r.bat run release -- --new-session --session morning-alpha --session-name "Morning Alpha"
```

Expected:

- Draxul starts normally.
- You land in a fresh shell session, not an old one.
- A startup toast says a new session was started.
- `--list-sessions` later shows `morning-alpha`.
- The display name should appear as `Morning Alpha`.

---

## Test 2 — Auto-generated session id

Command:

```powershell
r.bat run release -- --new-session --session-name "Morning Auto"
```

Expected:

- Draxul starts a fresh session.
- A startup toast says a new session was started.
- `--list-sessions` later shows a generated session id rather than `default`.
- The session name should appear as `Morning Auto`.

Notes:

- The generated id is intentionally timestamp-ish and ugly enough to be unique,
  because human beauty is not the first duty of an id.

---

## Test 3 — Detach on close, then live reattach

Start a session and create something recognizable:

```powershell
r.bat run release -- --session morning-alpha
```

Inside Draxul:

- create 2 workspaces
- split one workspace
- leave distinct commands or prompts visible in multiple panes

Then:

- close the main window with the titlebar `X`

Expected after close:

- Draxul window disappears
- shell work should stay alive in the background
- `--list-sessions` should show the session as `detached`

Reattach:

```powershell
r.bat run release -- --session morning-alpha
```

Expected after relaunch:

- the previous live layout comes back
- panes/workspaces are the same live session, not a reconstructed copy
- a toast says the live session was reattached

---

## Test 4 — Restore from saved topology after owner is gone

Use a named session:

```powershell
r.bat run release -- --session restore-check
```

Inside Draxul:

- create 2 workspaces
- split one workspace
- optionally rename a tab and a pane

Then close the window so the session detaches.

Now kill the running session owner explicitly:

```powershell
r.bat run release -- --session restore-check --kill-session
```

If you want to test saved restore instead of full deletion, do this variant:

1. Launch the session again after detaching
2. Close it cleanly via a normal app exit path that saves topology
3. Relaunch with the same `--session restore-check`

Expected for restore case:

- Draxul comes back with the saved tab/pane topology
- workspaces, split layout, focus, pane names, tab names, and cwd-based launch
  details should be restored
- a toast says the saved session was restored

Expected not to happen:

- no silent attach to some unrelated live session
- no blank fresh session if saved topology exists and is valid

---

## Test 5 — List sessions

Command:

```powershell
r.bat run release -- --list-sessions
```

Expected:

- each known session appears on its own line
- the line shows:
  - session id
  - state: `live`, `detached`, or `saved`
  - workspace count
  - pane count
  - display name when different from session id

Things to sanity-check:

- detached sessions show `detached`
- active owner-backed sessions prefer live counts
- restored-only sessions still appear even if no owner is running

---

## Test 6 — Explicit detach

Launch a named session:

```powershell
r.bat run release -- --session detach-check
```

Then from another terminal:

```powershell
r.bat run release -- --session detach-check --detach-session
```

Expected:

- the running Draxul window hides
- the session remains alive
- `--list-sessions` shows it as `detached`

---

## Test 7 — Explicit attach

After Test 6:

```powershell
r.bat run release -- --session detach-check --attach-session
```

Expected:

- the detached session becomes the visible one
- layout/state matches the detached session

Note:

- A normal `r.bat run release -- --session detach-check` should also be enough
  in most cases; this test is specifically for the explicit control path.

---

## Test 8 — Rename session

Command:

```powershell
r.bat run release -- --session morning-alpha --rename-session --session-name "Renamed Morning"
```

Expected:

- command succeeds
- `--list-sessions` shows the updated display name
- rename should work for either a running session or a saved-only one

---

## Test 9 — Kill session

Command:

```powershell
r.bat run release -- --session morning-alpha --kill-session
```

Expected:

- a running session is terminated, or a saved-only session is deleted
- `--list-sessions` no longer shows it

Good follow-up:

- run `--kill-session` again on the same id and confirm it fails cleanly rather
  than pretending success

---

## Test 10 — Session picker basic flow

Command:

```powershell
r.bat run release -- --pick-session
```

Expected:

- Draxul opens into a session picker surface instead of a shell workspace
- existing sessions are listed
- the top row is `new-session`
- if there are existing sessions and no query text, the first real session is
  selected by default

Try:

- press `Down` / `Up`
- press `F5`
- press `Esc`

Expected:

- selection moves
- refresh does not explode
- escape closes the picker

---

## Test 11 — Picker creates a new session

Command:

```powershell
r.bat run release -- --pick-session
```

Inside picker:

- type `Picker Session`
- ensure the top row reads like `new-session Picker Session`
- press `Enter`

Expected:

- picker exits
- a new Draxul session launches
- startup toast says a new session was started
- `--list-sessions` later shows the new session with display name
  `Picker Session`

---

## Test 12 — Picker attaches or restores an existing session

Command:

```powershell
r.bat run release -- --pick-session
```

Inside picker:

- select an existing session
- press `Enter`

Expected:

- picker exits
- Draxul opens the selected session
- if it was live and detached, it reattaches
- if only saved state existed, it restores

---

## Test 13 — Picker kills a session

Command:

```powershell
r.bat run release -- --pick-session
```

Inside picker:

- highlight a disposable test session
- press `Delete`

Expected:

- the session is removed
- the list refreshes
- the killed session disappears

Recommendation:

- do this only with obvious throwaway names unless you enjoy performance art

---

## Test 14 — Default session compatibility

Command:

```powershell
r.bat run release
```

Expected:

- the old default-session behavior still works
- if `default` is detached, it should reattach
- if `default` only has saved topology, it should restore
- normal users who do not care about named sessions should not have to learn a
  new religion

---

## Test 15 — Basic failure-path sanity

Try:

- `--new-session --session existing-id` where that session already exists
- `--rename-session` without `--session-name`
- combining two mutually-exclusive control modes, e.g.:

```powershell
r.bat run release -- --pick-session --list-sessions
```

Expected:

- clear errors
- no silent fallback into some unrelated mode

---

## Suggested morning order

If you want the fastest signal:

1. Test 1
2. Test 3
3. Test 5
4. Test 10
5. Test 11
6. Test 12
7. Test 13

If those pass, the overall shape is probably real and not just elaborate fan
fiction.

---

## Known limitations while testing

- This is still not a true headless session supervisor.
- Crash-reconnect is not the same as restore-from-saved-topology.
- Dead panes do not yet stay visible with remain-on-exit behavior.
- The picker currently lives behind `--pick-session`; it is not yet the default
  startup flow.
- Session actions are not yet fully integrated into the normal command palette.

---

## Cleanup commands

Useful after testing:

```powershell
r.bat run release -- --list-sessions
r.bat run release -- --session morning-alpha --kill-session
r.bat run release -- --session detach-check --kill-session
r.bat run release -- --session restore-check --kill-session
```

---

*Filed by: codex — 2026-04-09*
