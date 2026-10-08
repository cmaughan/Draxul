# Refresh Markdown fonts after display changes

**Summary:** Resize Markdown text when the window moves between displays so it keeps the intended readable size.

**Priority:** P1  
**Source:** `modules/markdown/draxul-markdown/src/markdown_host.cpp`

**Evidence and trigger:** B17; private fonts retain initialization density after the application updates shared fonts for a different display.

- [x] **Investigate:** Trace display-density delivery to private rich-text fonts and companion previews.
  - Confirmed on 2026-10-08: `App::on_display_scale_changed()` rebuilt only the shared
    `TextService` and then called `IHost::on_font_metrics_changed()`, which carries no
    density. `MarkdownHost` (standalone and Kanban companion previews alike) captured
    `HostContext::display_ppi` once at initialize and reused it for every later
    `RichTextService` rebuild, so its glyphs, row metrics, and scrollbar geometry kept
    the original display's density.
- [x] **Fix:** Propagate current density and rebuild fonts, metrics, layout, and draw state transactionally.
  - New `IHost::on_display_density_changed(float)` (default no-op), delivered by App to
    every pane host in every Space/tab before the shared metrics notification.
    `MarkdownHost` builds a replacement `RichTextService` at the new density first and
    swaps it in only on success, then marks layout and draw list dirty.
- [x] **Acceptance:** Moving between differently scaled displays updates text and hit geometry; failed rebuilding retains usable presentation.
  - `markdown host rebuilds private fonts when the display density changes` in
    `tests/markdown_parser_tests.cpp` moves standalone and companion hosts 96 -> 192 ->
    96 ppi, compares against a host created at 192 ppi, and checks that a rejected
    density keeps the previous fonts, rows, and glyphs.
- [ ] **Validation:** Verify display movement on available platforms; run core aggregate tests, relevant rendering checks, and same-cache smoke.
  - macOS Debug `do.py test debug` (56 CTest entries) passed apart from three load-sensitive flakes that pass in isolation or also fail on a pre-change binary, plus the known `draxul-do-py-tests` megacity AGENTS.md failure in submodule-less worktrees; `do.py smoke debug --skip-build` passed (2026-10-08). Physically moving
    the window between differently scaled displays was not possible in this headless
    run; still outstanding on macOS and Windows.

**Related gaps noticed (not fixed here):** `PersonalAssistantHost` also keeps its
initialize-time `display_ppi_` for its private `RichTextService`, and `PluginHost`
reports its initialize-time `display_ppi_` to plugins through `plugin_viewport()`.
Both can now override `on_display_density_changed()`.

## Windows display preflight (2026-10-08)

- Read-only native `EnumDisplayMonitors` / `GetMonitorInfoW` /
  `GetDpiForMonitor(MDT_EFFECTIVE_DPI)` inspection with a per-monitor-aware
  calling thread found `DISPLAY1` at `(0, 0)` and `DISPLAY2` at `(-3840, 0)`
  both at 168 DPI, and `DISPLAY3` at `(3840, 0)` at 192 DPI. All DPI calls
  returned success. Therefore a real different-density movement gate is
  available; same-DPI displays are not the blocker.
- After the parent's exclusive GPU slot is assigned, move an exact owned
  Markdown HWND from DISPLAY1 to DISPLAY3 and back using native window movement.
  Record actual `GetDpiForWindow` transitions, screen captures of the same text,
  and pointer selection/link/scrollbar geometry after each transition. Keep
  fixed-density render overrides out of this run. A separate target-density
  initial launch can provide a visual comparison if needed within the deadline.
- No window was launched or moved during preflight. Actual text/geometry
  validation remains outstanding; retain the unchecked validation gate.

## Windows native movement result (2026-10-08): PARTIAL

- [x] Real display-density/font transition: client PID `56100`, creation token
  `134359430754614952`, HWND `6033706`, route
  `ui-511c3e639830c47ad86b7dcd`; `GetDpiForWindow` recorded **168 -> 192 -> 168**
  while native `MoveWindow` moved DISPLAY1 -> DISPLAY3 -> DISPLAY1. No fixed
  render-density override was used. Captures visibly show larger Markdown
  headings/body text at 192 DPI and the original-sized presentation on return.
- [ ] Real pointer-geometry proof: bounded scrollbar drag attempts were sent
  at both densities, but the captured document did not demonstrably scroll.
  Do not count the input dispatch itself as proof of correct hit geometry;
  leave this part pending for a targeted follow-up. No further GPU retries.
- Stable evidence is
  `build-ninja-debug/windows-gates/core-20261008/dg63sobtye/`:
  `density-168.bmp`, `density-192.bmp`, `density-192-scroll.bmp`,
  `density-168-return-scrolled.bmp`, `density-168-return-top.bmp`,
  `events.jsonl`, and complete client/loader logs. Exact PID/HWND/route guards
  surrounded native operations. Normal client exit was **0** and its isolated
  server shutdown returned 0; subsequent inspection found no endpoint/process.
- Parent's app shards, five snapshots, and isolated same-cache smoke passed;
  aggregate remains 73/79 with separately owned failures. This card remains
  pending for pointer geometry, not display availability. GPU/pointer ownership
  was released after this bounded attempt so other product gates could proceed.
