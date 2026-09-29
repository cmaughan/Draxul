# Diagnose macOS diagnostics-panel snapshot drift

Found during validation of `kanban/done/04 larger-kanban-preview -feature.md`
on 2026-09-29. `draxul-render-panel-view` fails against the macOS reference:
19.7081% changed pixels versus 18% tolerance (1.21s), repeated after the core
aggregate finished at 19.799% (1.19s). Cause is not established; the scenario
contains live timing counters, but the drift must be inspected rather than
attributed to those counters without evidence. No references were updated.
The separate Markdown and Kanban preview render exports passed visual inspection.

- [x] Inspect `tests/render/out/panel-view.macos.actual.bmp` and the reference/diff
  to identify the changed regions and establish whether this is rendering,
  environment, or expected diagnostics-content variation.
- [x] Correct the cause or stabilize appropriate dynamic regions without masking
  genuine layout regressions.
- [x] Pass the panel-view CTest and same-cache startup smoke.

Logs: `/tmp/draxul-preview-panel.log` and
`/tmp/draxul-preview-panel-recheck.log`.

**Resolution (2026-09-29):** Inspected the actual/reference/diff images.
The existing macOS reference is from Debug and includes the red Debug-build
warning, which changes the panel's rows; the Release render has no warning
and different timing values. The top terminal content also shifted with the
current window chrome, but remained aligned with the current source. The
Debug snapshot continued to pass. The render harness now prefers a
platform-specific `.release.bmp` reference when present; the original Debug
reference is retained. CMake accepts optional build-mode suffixes, and render
shortcuts accept `release --skip-build` so blessing the intended cache is
unambiguous. The Release reference was blessed using that command.

**Validation:** `draxul-render-panel-view` passed on Debug (1.76 s) and
Release (1.22 s); the Release comparison changed only 0.336% of pixels
against the new reference, below the unchanged 18% threshold. The core +
SatView Release aggregate passed 28/28 CTest entries in 33.76 s, including
`draxul-do-py-tests`; same-cache Release startup smoke passed. The Debug
core + SatView aggregate passed 28/28 earlier (41.89 s) and Debug startup
smoke passed. `python3 do.py hygiene` passed. No extra Linux/Windows render
reference was generated here.
