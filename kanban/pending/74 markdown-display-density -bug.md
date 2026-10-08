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
  - Aggregate tests and smoke: see the commit's validation summary. Physically moving
    the window between differently scaled displays was not possible in this headless
    run; still outstanding on macOS and Windows.

**Related gaps noticed (not fixed here):** `PersonalAssistantHost` also keeps its
initialize-time `display_ppi_` for its private `RichTextService`, and `PluginHost`
reports its initialize-time `display_ppi_` to plugins through `plugin_viewport()`.
Both can now override `on_display_density_changed()`.
