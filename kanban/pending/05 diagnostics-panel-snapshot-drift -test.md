# Diagnose macOS diagnostics-panel snapshot drift

Found during validation of `kanban/done/04 larger-kanban-preview -feature.md`
on 2026-09-29. `draxul-render-panel-view` fails against the macOS reference:
19.7081% changed pixels versus 18% tolerance (1.21s), repeated after the core
aggregate finished at 19.799% (1.19s). Cause is not established; the scenario
contains live timing counters, but the drift must be inspected rather than
attributed to those counters without evidence. No references were updated.
The separate Markdown and Kanban preview render exports passed visual inspection.

- [ ] Inspect `tests/render/out/panel-view.macos.actual.bmp` and the reference/diff
  to identify the changed regions and establish whether this is rendering,
  environment, or expected diagnostics-content variation.
- [ ] Correct the cause or stabilize appropriate dynamic regions without masking
  genuine layout regressions.
- [ ] Pass the panel-view CTest and same-cache startup smoke.

Logs: `/tmp/draxul-preview-panel.log` and
`/tmp/draxul-preview-panel-recheck.log`.
