# WI 130 — Keybinding Inspector

**Type:** Feature  
**Severity:** Low (developer/power-user ergonomic)  
**Source:** Gemini review  
**Authored by:** claude-sonnet-4-6

---

## Problem / Motivation

When a keybinding doesn't work as expected, there is no way to see which action matched (or why no action matched). Power users who configure `[keybindings]` in `config.toml` have no feedback loop.

From Gemini: "A keybinding inspector that shows which action matched and why."

---

## Proposed UX

**Mode A — Inline key event log in the diagnostics panel:**
When the diagnostics panel is open, the last 10 key events are shown with:
- Raw SDL scancode + modifiers
- The Draxul action name that was dispatched (or `<none>` / `<forwarded to nvim>`)
- Whether the event was consumed by a GUI action or passed through to the host

**Mode B — Temporary inspect mode:**
A `toggle_keybinding_inspector:` action activates a brief overlay on the next keypress that shows the decoded action for that key, then dismisses itself. Similar to macOS Accessibility Inspector.

MVP: Mode A (log in diagnostics panel) is simpler and more maintainable.

---

## Implementation Notes

- `InputDispatcher` already resolves keybindings — add a `last_key_dispatch_result_` debug field
- `DiagnosticsHost` can read and render this field
- No changes to the keybinding resolution logic — purely observational
- Ensure the inspect log doesn't fire for its own keybinding (filter `toggle_keybinding_inspector:` event from log)

---

## Implementation Steps

- [ ] Add `KeyDispatchRecord { KeyEvent raw; std::string action_name; bool consumed; }` to `InputDispatcher`
- [ ] Record the last N key dispatches in a ring buffer (N=10)
- [ ] Expose via `InputDispatcher::debug_state()` or a dedicated accessor
- [ ] Render in `DiagnosticsHost` panel under a "Key Events" section
- [ ] Document in `docs/features.md`

---

## Acceptance Criteria

- [ ] Diagnostics panel shows last 10 key events with action names
- [ ] Key events forwarded to Neovim show `<forwarded>` not `<none>`
- [ ] Unbound keys show `<none>`
- [ ] No performance impact when diagnostics panel is closed
- [ ] CI green

---

## Interdependencies

- None blocking; can be done independently.
- Complements **WI 27** (configurable keybindings) — inspector is the debug tool for the feature.
