# WI 129 — Reopen Last Closed Pane or Tab

**Type:** Feature  
**Severity:** Low–Medium (common terminal ergonomic feature)  
**Source:** Gemini review  
**Authored by:** claude-sonnet-4-6

---

## Problem / Motivation

When a user accidentally closes a pane or workspace tab there is no way to reopen it. Modern terminals (iTerm2, WezTerm, Zellij) support "reopen last closed pane" as a standard keybinding. Gemini listed this as a top QoL improvement: "Reopen the last closed pane or tab."

---

## Proposed Design

### Pane Close
1. When a pane is closed (`HostManager::close_leaf()`), push a `ClosedPaneRecord` onto a bounded stack (max depth: 10):
   ```cpp
   struct ClosedPaneRecord {
       HostKind kind;           // nvim, shell, megacity, etc.
       std::string cwd;         // OSC 7 last reported CWD
       std::string tab_name;    // workspace tab name
       SplitRatio ratio;        // approximate split geometry hint
   };
   ```
2. GUI action `"reopen_last_pane:"` pops the top record and creates a new pane with those parameters.

### Tab Close
1. When a workspace tab is closed, push a `ClosedTabRecord` with all pane records.
2. GUI action `"reopen_last_tab:"` pops and recreates the tab.

### Keybinding
- Default: `Cmd+Shift+T` (macOS convention) or configurable under `[keybindings]`.

---

## Implementation Notes

- `ClosedPaneRecord` stack lives in `App` (or `HostManager`) — small, bounded, no persistence required for MVP
- For session persistence integration, the record format can be reused by **WI 25** (session restore)
- CWD only available if the pane reported OSC 7; gracefully fall back to default CWD otherwise
- Shell host reopens a fresh shell in the last CWD (not a replay of the session)
- NvimHost reopens nvim in the last CWD

---

## Acceptance Criteria

- [ ] Closing a pane then invoking `reopen_last_pane:` opens a new pane of the same kind in the same CWD
- [ ] Closing a tab then invoking `reopen_last_tab:` reopens all panes in the tab
- [ ] Reopening from an empty stack is a no-op (no crash)
- [ ] Stack depth capped at 10 (oldest record dropped)
- [ ] Default keybinding documented in `docs/features.md`
- [ ] CI green

---

## Interdependencies

- **WI 128** (tab name editing) — store the user-assigned tab name in `ClosedTabRecord`
- **WI 25** (session restore) — `ClosedPaneRecord` format can be shared
