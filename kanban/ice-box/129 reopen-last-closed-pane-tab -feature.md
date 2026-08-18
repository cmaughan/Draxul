# WI 129 — Reopen Last Closed Pane or Tab

**Type:** Feature  
**Severity:** Low–Medium (common terminal ergonomic feature)  
**Source:** Gemini review  
**Authored by:** claude-sonnet-4-6

---

## Problem / Motivation

When a user accidentally closes a pane or tab there is no way to reopen it. Modern terminals (iTerm2, WezTerm, Zellij) support "reopen last closed pane" as a standard keybinding. Gemini listed this as a top QoL improvement: "Reopen the last closed pane or tab."

---

## Proposed Design

### Pane Close
1. When a pane is closed (`PaneManager::close_leaf()`), push a `ClosedPaneRecord` onto a bounded stack (max depth: 10):
   ```cpp
   struct ClosedPaneRecord {
       std::string provider_id; // built-in provider or plugin manifest id
       std::string launch_descriptor; // bounded provider-owned JSON
       std::string cwd;         // OSC 7 last reported CWD
       std::string tab_name;
       SplitRatio ratio;
   };
   ```
2. GUI action `"reopen_last_pane:"` pops the top record and creates a new pane with those parameters.

### Tab Close
1. When a tab is closed, push a `ClosedTabRecord` with all pane records.
2. GUI action `"reopen_last_tab:"` pops and recreates the tab.

### Keybinding
- Default: `Cmd+Shift+T` (macOS convention) or configurable under `[keybindings]`.

---

## Implementation Notes

- The bounded undo record belongs to the authoritative server Session so all
  clients observe the same reopen result.
- Reopen is a version-checked topology mutation that creates a fresh
  server-owned process from the saved launch descriptor; it does not resurrect
  the exited process.
- CWD only available if the pane reported OSC 7; gracefully fall back to default CWD otherwise
- Shell host reopens a fresh shell in the last CWD (not a replay of the session)
- Client-owned hosts and plugins reopen only when their provider is still available;
  unavailable providers produce a clear non-destructive error.

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

- Tab naming and server-authoritative session descriptors are already delivered; reuse
  their canonical values instead of defining a parallel host-kind format.
