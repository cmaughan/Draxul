# WI 128 — Workspace Tab Name Editing

**Type:** Feature  
**Severity:** Low–Medium (high user visibility, polish)  
**Source:** Claude review, Gemini review  
**Authored by:** claude-sonnet-4-6

---

## Problem / Motivation

Workspace tabs have a `name` field in `app/workspace.h` but there is currently no UI to rename a tab. Users are stuck with the auto-generated name. Both Claude and Gemini listed this as a top QoL improvement.

From Claude: "Workspace tabs have a `name` field in `app/workspace.h` but no UI to edit it. Allow double-clicking a tab to rename it; use OSC 7 working directory as a default name."

---

## Proposed UX

1. **Double-click** a tab in the tab bar → inline text-edit field appears in the tab
2. User types a name; **Enter** confirms, **Escape** cancels
3. Alternatively: right-click context menu with "Rename Tab" option (simpler first step)
4. Default name when tab is first created: derived from the active pane's host name + working directory (via OSC 7 if available)
5. Names persisted in session state (related to **WI 25** session restore)

---

## Implementation Notes

- `Workspace` already has a `name` field — just wire up the setter
- `ChromeHost` renders the tab bar — tab hit detection is already present (see **WI 119**)
- Inline editing could use an ImGui `InputText` overlay on top of the tab, or a simple SDL text-input event handler in `ChromeHost`
- OSC 7 default: `LocalTerminalHost` already receives OSC 7 → wire `on_osc7_cwd` to update the tab name if no user override has been set
- Command palette action `"rename_tab:"` for keyboard-driven rename

---

## Acceptance Criteria

- [x] Double-click (or right-click → Rename) opens a rename UI
- [x] Enter confirms; Escape cancels with no change
- [x] Tab name persists across workspace switches
- [ ] Tab name saved/restored with session state — deferred until WI 25 (session restore) lands. In-memory persistence only for now.
- [x] CI green; no render regression

## Completion Notes

Implemented and shipped on 2026-04-08. Scope expanded beyond the original tab-only ask:

- **Inline tab rename**: double-click any tab pill or `Ctrl+S, ,` (tmux-style chord) begins an in-place edit. Enter / Escape / Backspace / Delete / Home / End / Left / Right all behave as expected. Empty commits leave the existing name untouched.
- **Inline pane rename**: double-click any pane status pill or `Ctrl+S, .` begins a per-pane override edit. Empty commit clears the override and reverts to `host->status_text()`. Per-leaf overrides stored in `HostManager::pane_user_names_`.
- **OSC 7 default naming**: shell hosts feed the workspace tab name from OSC 7 cwd until the user explicitly renames the tab; once `name_user_set` is true, OSC 7 updates no longer overwrite.
- **Luminance-based pill text colour**: text foreground is chosen via BT.709 relative luminance against the underlying NanoVG fill, so any future pill colour tweak gets readable ink without hand-tuning.
- **Atlas one-frame-late fix**: glyphs typed during a rename are warmed and flushed in `on_rename_text_input` (between frames) rather than during `chrome_host.draw()`, because the Metal renderer flushes pending atlas uploads inside `begin_frame()` — uploads queued from a draw pass are one frame late.
- **Tests**: 13 cases / 60 assertions in `tests/chrome_host_rename_tests.cpp` covering both tab and pane state machines (typing, Enter, Escape, Backspace, empty commit, switching targets, ignored input when no session active).
- **Default keybindings**: `rename_tab = Ctrl+S, ,` and `rename_pane = Ctrl+S, .` mirror tmux's `<prefix> ,`. Default keybinding count bumped 34 → 36.

---

## Interdependencies

- **WI 129** (reopen last closed pane/tab) uses the tab name when re-creating the tab — coordinate to ensure name is stored in the close record.
- **WI 119** (ChromeHost tab-bar hit-test) must be complete for confidence in double-click detection.
