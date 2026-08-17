# Tab and pane color labels

**Type:** feature
**Priority:** 59
**Raised by:** GPT/Codex

## User need

Assign a small color and optional short tag to tabs/panes so large mixed sessions are easier to scan.

## Implementation plan

- [ ] Add optional semantic color/tag metadata to tab and pane controller records and versioned session serialization.
- [ ] Define an accessible named palette derived from Chrome theme roles; allow custom RGB only if config/schema support can validate contrast.
- [ ] Add palette actions and a compact Chrome editor for set/change/clear, with live preview and UTF-8 length limits for tags.
- [ ] Render a narrow color marker and short tag in top-bar tabs and pane pills without changing hit-test geometry unexpectedly.
- [ ] Compute foreground/outline contrast for light/dark themes and accessibility mode; color must not be the only identity cue.
- [ ] Include labels in tab/pane switcher completion metadata and preserve them through move/clone/template/session operations.

## Tests and acceptance

- [ ] Round-trip colors/tags through sessions and controller operations, including unknown/invalid legacy values.
- [ ] Test Chrome layout/hit testing at minimum widths, 1x/2x DPI, Unicode tags, and high-contrast palettes.
- [ ] Labels do not overlap close/rename controls and do not make names inaccessible.
- [ ] Metal/Vulkan render snapshots remain aligned.

## Dependencies and parallelism

Depends on finishing the relevant ownership in
`kanban/ice-box/22 app-tab-session-controllers -refactor.md`; Chrome layout extraction
is complete in `kanban/done/23 chrome-layout-render-editing -refactor.md`.

<model>GPT-5 Codex</model>
