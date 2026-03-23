# 22 bracketed-paste-confirmation -feature

**Priority:** LOW
**Type:** Feature (safety, standard terminal behavior)
**Raised by:** Claude
**Model:** claude-sonnet-4-6

---

## Problem

Pasting more than a few lines into a terminal pane without confirmation is a well-known footgun — users accidentally execute multi-line clipboard content when they intended to paste text. Modern terminal emulators (iTerm2, Ghostty, Warp) show a confirmation banner for large pastes. Draxul has no such guard.

---

## Implementation Plan

- [ ] Read the paste handling path in `app/app.cpp` and/or `libs/draxul-host/` to understand how clipboard content is currently read and sent to the active host.
- [ ] Decide on the confirmation trigger:
  - Default: paste ≥ 5 lines requires confirmation (make configurable).
  - Add `[paste] confirm_lines = 5` to `config.toml`.
- [ ] Design the confirmation UX:
  - A single-line banner appears at the bottom of the affected pane: `Paste N lines? [y/n]`
  - `y` / `Enter`: proceed with paste.
  - `n` / `Escape`: cancel; clipboard content is discarded.
  - The banner is rendered using the ImGui overlay or the existing pane decoration layer — whichever is simpler.
- [ ] Implementation steps:
  - [ ] Add `PasteConfig { int confirm_lines; }` to `AppConfig`.
  - [ ] In the paste dispatch path: count newlines in the clipboard string. If `count >= confirm_lines`, show the confirmation UI instead of pasting immediately.
  - [ ] Implement the confirmation state in the active host or app layer.
  - [ ] On confirm: send the clipboard to the host as normal.
  - [ ] On cancel: no-op.
- [ ] Build and run smoke test.

---

## Acceptance

- Pasting fewer than `confirm_lines` lines proceeds immediately (no regression).
- Pasting `confirm_lines` or more shows the banner; `y` sends the content, `n` discards it.
- `confirm_lines = 0` or very high values effectively disables the guard.

---

## Interdependencies

- No upstream blockers; self-contained.

---

*claude-sonnet-4-6*
