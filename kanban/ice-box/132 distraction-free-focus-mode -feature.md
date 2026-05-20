# WI 132 — Distraction-Free / Focus Mode

**Type:** Feature  
**Severity:** Low (polish, power-user ergonomic)  
**Source:** Gemini review  
**Authored by:** claude-sonnet-4-6

---

## Problem / Motivation

When writing or reading in a terminal or Neovim pane, the chrome (tab bar), toast notifications, and diagnostics panel are visual noise. A temporary focus mode that hides all overlays and focuses on the active pane improves the writing/reading experience.

From Gemini: "A temporary focus mode that hides chrome, toasts, and diagnostics."

---

## Proposed UX

- GUI action `"toggle_focus_mode:"` — keybinding e.g. `Cmd+Shift+F` (configurable)
- Entering focus mode:
  - Hides `ChromeHost` (tab bar)
  - Suppresses new `ToastHost` toasts (existing ones fade out immediately)
  - Hides `DiagnosticsHost`
  - Active pane expands to fill the full window
- Exiting focus mode:
  - All overlays restored to previous visibility state
  - Toast buffer replayed if any were suppressed
- Visual indicator: brief "Focus mode" toast on enter/exit (ironic, but standard practice)

---

## Implementation Notes

- Add `bool focus_mode_active_` to `App`
- In the render loop, skip rendering hidden overlays when focus mode is active
- `OverlayRegistry` (WI 125) would make this trivial — each overlay has `set_visible(bool)`. Without WI 125, set individual flags on each overlay host
- Window title can include `[Focus]` suffix as a secondary indicator
- Focus mode state is session-local (not persisted)

---

## Implementation Steps

- [ ] Add `toggle_focus_mode:` to `GuiActionHandler` and wire keybinding
- [ ] Add `focus_mode_active_` flag to `App`
- [ ] Suppress overlay rendering when flag is set
- [ ] On exit, restore overlays
- [ ] Suppress toasts (not drop them — replay suppressed toasts on exit)
- [ ] Document in `docs/features.md`

---

## Acceptance Criteria

- [ ] `toggle_focus_mode:` hides tab bar, toasts, diagnostics
- [ ] Active pane fills the window
- [ ] Toggling off restores previous layout
- [ ] Toasts generated during focus mode appear after exit
- [ ] `docs/features.md` updated
- [ ] CI green

---

## Interdependencies

- **WI 125** (overlay registry) would simplify the implementation significantly — consider doing this after WI 125.
- **WI 120** (toast lifecycle test) should verify toast suppression/replay behaviour.
