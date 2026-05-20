# 13 diagnostics-panel-render -test

**Priority:** LOW-MEDIUM
**Type:** Test
**Raised by:** Gemini
**Model:** claude-sonnet-4-6

---

## Problem

The diagnostics panel (ImGui bottom panel) is toggled via a keybinding. There is no render snapshot test that verifies the panel renders without artefacts when visible. Panel-related rendering bugs (layout, atlas interaction, panel stealing grid rows) are currently only catchable by visual inspection.

---

## Implementation Plan

- [ ] Read `py do.py` and related render test infrastructure to understand how render snapshots are blessed and compared.
- [ ] Read `app/app.cpp` or `app/diagnostics_panel.cpp` (if it exists) to understand how the panel is toggled and what state it needs.
- [ ] Add a new render test scenario `draxul-render-diagnostics`:
  - Launch Draxul with render-test flag.
  - Toggle diagnostics panel on (via keybinding simulation or a render-test command).
  - Render a frame.
  - Capture BMP snapshot.
  - Compare against blessed reference.
- [ ] Bless the initial reference: `py do.py blessdiagnostics` (or equivalent new blessing target).
- [ ] Add the scenario to the CI render test suite.
- [ ] Run smoke test and render tests to confirm the new scenario integrates correctly.

---

## Acceptance

- A render snapshot of the diagnostics-panel-visible state is committed and passing in CI.
- Subsequent changes that corrupt panel rendering fail the snapshot test automatically.

---

## Interdependencies

- Requires the render test infrastructure (already in place).
- No upstream code dependencies; this is purely additive.
