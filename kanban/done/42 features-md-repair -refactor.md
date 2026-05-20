# 42 features-md-repair -refactor

*Filed by: claude-sonnet-4-6 — 2026-03-29*
*Source: review-latest.gpt.md [P]*

## Problem

`docs/features.md` is the canonical reference for what Draxul already supports.  `CLAUDE.md`
instructs agents to:

> "Before creating new work items, check `docs/features.md` to verify the proposed feature or
>  capability is not already implemented."

[P] reports the file is "malformed around diagnostics, keybindings, and config tables" at
HEAD fffa7af.  If the document is incorrect, agents will file duplicate work items for
already-implemented features, wasting effort and creating noise in the work-item backlog.

## Acceptance Criteria

- [x] `docs/features.md` accurately reflects the current feature set as implemented in the
      codebase at the time of this fix.
- [x] The diagnostics section correctly describes the diagnostics panel (F12, ImGui overlay,
      per-step startup timings, frame stats, atlas usage).
- [x] The keybindings section accurately describes all configurable GUI keybinding actions
      (`toggle_diagnostics`, `copy`, `paste`, `font_increase`, `font_decrease`, `font_reset`,
      and any others that exist).
- [x] The config tables accurately reflect `config.toml` sections and keys.
- [x] After the repair, the document is parseable as valid Markdown (no broken tables,
      missing closing fences, or malformed heading structure).

## Implementation Plan

1. Read `docs/features.md` in full to identify broken or inaccurate sections.
2. Read the relevant source files (`app/gui_action_handler.cpp`, `app/app.cpp`,
   `libs/draxul-nvim/src/`, `config.toml` defaults) to verify the current feature state.
3. Repair the malformed sections.
4. Update any features that have been added since the last `docs/features.md` update but are
   not yet documented (check recent completed work items).
5. Commit with message: `docs: repair and sync features.md to current codebase state`.

## Files Likely Touched

- `docs/features.md`

## Interdependencies

- **This should land early** — it unblocks correct work-item filing for all future review
  cycles (the document is a coordination tool, not just user documentation).
- Independent of other open WIs at the code level.
- After landing, add a note to `CLAUDE.md` validation checklist to run a docs sync pass after
  every significant feature addition.
