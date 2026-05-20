# WI 135 — tmux-replacement roadmap

**Type:** feature  
**Priority:** High  
**Source:** user request — 2026-04-09  
**Created:** 2026-04-09  
**Produced by:** codex

---

## Problem

Draxul already covers part of the tmux value proposition: split panes, workspace
tabs, pane focus/navigation, pane zoom, shell hosts on macOS/Windows, OSC 7 cwd
tracking, OSC 52 clipboard, keyboard copy mode, and configurable keybindings.

That is enough for "terminal with panes", but not yet enough for "daily tmux
replacement". tmux becomes indispensable because it provides:

1. Long-lived sessions that outlive any single client window
2. Reliable reattach/recovery after closing the UI or losing the frontend
3. Fast keyboard-first pane/window/session management
4. Searchable/capturable history and coordinated multi-pane workflows
5. A session model that is scriptable and stable across platforms

The missing pieces are mostly product-level session and workflow features rather
than renderer work.

---

## Target Definition

For this roadmap, "useful as a replacement to tmux on both Windows and macOS"
means:

- A user can create named sessions containing tabs and panes running shell hosts.
- Those sessions can survive window close, app restart, and recoverable crashes.
- A later Draxul instance can list and reattach to live sessions.
- Common tmux workflows are keyboard-first: split, move, resize, zoom, rename,
  respawn, broadcast input, inspect history, and switch sessions quickly.
- The same mental model works on macOS PTY and Windows ConPTY backends.

This does **not** require strict tmux command-language parity or every tmux
option before shipping a useful first version.

---

## Existing Foundation

The following capabilities already reduce scope and should be treated as inputs,
not new work:

- Split panes and draggable dividers
- Workspace tabs
- Keyboard pane focus and resize actions
- Pane zoom, restart host, close pane, swap pane
- Inline tab/pane rename
- OSC 7 cwd tracking
- OSC 52 clipboard integration
- Keyboard copy mode
- Command palette and configurable GUI keybindings

Related open work items already cover adjacent gaps:

- WI 37 — duplicate pane
- WI 38 — pane activity badges
- WI 39 — right-click context menus
- WI 40 — scrollback buffer completion

---

## Required Feature Areas

### Phase 1 — Persistent session core (MVP-critical)

Without this, Draxul is not a tmux replacement.

- [ ] Introduce a first-class `SessionManager` / supervisor layer that owns
      long-lived shell hosts independently of one GUI window instance.
- [ ] Define stable session ids, session names, and pane ids.
- [ ] Persist session topology: tabs, split tree, focused pane, pane names, tab
      names, cwd, launch command/profile, and environment/profile metadata.
- [ ] Support attach/detach semantics:
      - closing a client window detaches instead of destroying the session
      - launching Draxul can attach to an existing session
      - a crashed client can reconnect to the same live session
- [ ] Decide the control channel design:
      - local domain socket on macOS
      - named pipe or equivalent local IPC on Windows
- [ ] Keep backend process ownership cross-platform:
      - macOS: PTY/process stays alive under session supervisor
      - Windows: ConPTY/process tree stays alive under session supervisor
- [ ] Define shutdown policy:
      - explicit "kill session"
      - explicit "detach client"
      - clear UX distinction so accidental window close does not kill work
- [ ] Headless app lifecycle (no-window-open persistence):
      - macOS (Ghostty model): app stays in Dock with no windows. Closing
        a window detaches the session; clicking the Dock icon reopens a
        window (session picker or last session). Cmd+Q quits and kills
        sessions. Implemented via NSApplicationShouldTerminateAfterLastWindowClosed
        → NO, and applicationShouldHandleReopen to create a window.
      - Windows: system tray icon when all windows closed. Right-click
        menu: list sessions, new session, attach, quit. Closing a window
        minimizes to tray (detach). Implemented via Shell_NotifyIcon.

### Phase 2 — Session browser, restore, and failure UX (MVP-critical)

- [ ] Add a startup/session picker UI showing:
      - running sessions
      - detached sessions
      - last-attached timestamp
      - tab/pane counts
- [ ] Add command-palette entries and CLI flags for:
      - `new-session`
      - `attach-session`
      - `detach-session`
      - `kill-session`
      - `list-sessions`
- [ ] Restore tab names, pane names, split ratios, cwd, and launch descriptors
      when reconnecting to a persisted session.
- [ ] Add dead-pane/remain-on-exit behaviour:
      - if a shell exits, show exit code and recent output instead of silently
        destroying the pane
      - offer respawn/restart from UI and keybinding
- [ ] Add crash recovery messaging so the user understands whether they are
      reconnecting to a live session or restoring from saved state only.

### Phase 3 — tmux-grade pane/window management (MVP-critical)

Draxul already has the basics; it still needs the workflows users hit dozens of
times per day.

- [ ] Finish `duplicate_pane` (WI 37).
- [ ] Add move/join/break operations:
      - move pane to new tab
      - pull pane from another tab into current split tree
      - reorder tabs left/right
- [ ] Add layout presets and normalization:
      - even-horizontal
      - even-vertical
      - tiled
      - main-horizontal / main-vertical
- [ ] Add pane rotation / swap-target operations beyond current "swap with next".
- [ ] Add numeric quick-switching for tabs/sessions in addition to cycling.
- [ ] Add "new pane/tab at same cwd" and explicit host/profile targeting.
- [ ] Add session-level rename and quick switch surfaces.

### Phase 4 — History, search, and buffer capture (MVP-critical)

tmux users rely heavily on history surviving layout changes and being searchable.

- [ ] Finish WI 40 (scrollback completion) first.
- [ ] Add incremental search over visible grid + scrollback:
      - forward/backward search
      - next/previous match
      - highlight matches in copy mode
- [ ] Add `capture-pane` style export:
      - copy visible screen
      - copy full scrollback
      - save recent output to file
- [ ] Preserve scrollback across detach/reattach for persistent sessions.
- [ ] Add quick-open handling for URLs/file paths in scrollback.

### Phase 5 — Coordinated multi-pane workflows (Important)

- [ ] Add synchronize/broadcast input mode for a tab or selected pane group.
- [ ] Finish pane activity/bell tracking (WI 38) and surface it at tab/session
      level so background work is visible.
- [ ] Add per-pane unread-output markers and "jump to active pane" actions.
- [ ] Add notifications for background process exit/bell/activity with platform-
      appropriate behaviour on macOS and Windows.

### Phase 6 — Automation and startup manifests (Important)

- [ ] Add declarative session manifests (TOML) for reproducible workspaces:
      tabs, splits, commands, cwd, names, optional env vars.
- [ ] Add "save current session as manifest" and "open manifest" actions.
- [ ] Expose a minimal external control surface:
      - create session
      - attach
      - list
      - send text/command
- [ ] Keep this intentionally smaller than tmux scripting at first; prioritize
      stable automation for local workflows and editor integration.

### Phase 7 — Cross-platform correctness and validation (MVP-critical)

- [ ] Add tests covering attach/detach/session lifecycle without a real window.
- [ ] Add Windows-specific tests for ConPTY session persistence semantics.
- [ ] Add macOS-specific tests for PTY lifetime across client reconnect.
- [ ] Add smoke tests for:
      - create session -> detach -> reattach
      - split/layout restore
      - dead pane -> respawn
      - broadcast input
      - scrollback/search after reattach
- [ ] Ensure no shutdown path regresses into blocking the UI when killing or
      detaching session-owned children.

---

## Recommended Delivery Order

1. **Session supervisor + attach/detach**  
   This is the real tmux replacement threshold.
2. **Session browser + restore UX**  
   Makes the session core usable for humans.
3. **Scrollback completion + search/capture**  
   Needed so reattached panes are actually useful.
4. **Pane/window management completion**  
   Move/join/layout/break workflows close the daily-usage gap.
5. **Broadcast/activity/notifications**  
   Important for pair/admin/repetitive workflows.
6. **Manifest + CLI automation**  
   Makes the feature set durable and scriptable.

---

## Architecture Notes

- [ ] Keep app orchestration thin. The persistent-session machinery should live
      in a reusable library rather than being buried in `app/`.
- [ ] Avoid baking tmux semantics into renderer or windowing code. Session state
      should be host/process centric and testable headlessly.
- [ ] Treat Neovim host support as a follow-on. The first tmux-replacement goal
      is shell-host sessions; nvim embed can layer on later once the lifecycle
      model is proven.
- [ ] Prefer a single shared session model across Windows/macOS with platform-
      specific process/IPC backends hidden below the interface.

---

## Non-Goals For The First Cut

- [ ] Full tmux command language compatibility
- [ ] Remote multi-user session sharing
- [ ] Every tmux status-line customization primitive
- [ ] Plugins/hooks ecosystem before the session core exists

---

## Acceptance Criteria

- [ ] A named shell session can be created, detached, listed, and reattached on
      both macOS and Windows.
- [ ] Reattaching restores the live tab/pane topology, titles, split ratios,
      cwd metadata, and scrollback.
- [ ] Closing a Draxul window does not destroy a persistent session unless the
      user explicitly kills it.
- [ ] Dead panes remain inspectable and can be respawned.
- [ ] Users can search pane history and export/copy pane buffers.
- [ ] Users can broadcast input to multiple panes.
- [ ] Core attach/detach/session lifecycle is covered by automated tests on both
      platforms.
- [ ] Existing split-pane and host shutdown smoke tests still pass.

---

## Interdependencies

- [ ] WI 37 — duplicate pane
- [ ] WI 38 — pane activity badges
- [ ] WI 39 — right-click context menus (helpful for discoverability, not a hard blocker)
- [ ] WI 40 — scrollback buffer completion
- [ ] Existing HostManager lifecycle/split tests should be extended before large
      session-state refactors land.

---

## Suggested Follow-Up Work Item Split

Once this roadmap is approved, split it into implementation-sized items:

- [ ] `session-supervisor-core`
- [ ] `session-ipc-macos-windows`
- [ ] `session-browser-and-cli`
- [ ] `session-restore-and-dead-pane`
- [ ] `layout-presets-and-pane-move-ops`
- [ ] `copy-mode-search-and-capture`
- [ ] `broadcast-input-and-activity-routing`
- [ ] `session-manifest-import-export`
- [ ] `session-lifecycle-cross-platform-tests`
