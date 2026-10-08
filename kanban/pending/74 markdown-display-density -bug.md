# Refresh Markdown fonts after display changes

**Summary:** Resize Markdown text when the window moves between displays so it keeps the intended readable size.

**Priority:** P1  
**Source:** `modules/markdown/draxul-markdown/src/markdown_host.cpp`

**Evidence and trigger:** B17; private fonts retain initialization density after the application updates shared fonts for a different display.

- [ ] **Investigate:** Trace display-density delivery to private rich-text fonts and companion previews.
- [ ] **Fix:** Propagate current density and rebuild fonts, metrics, layout, and draw state transactionally.
- [ ] **Acceptance:** Moving between differently scaled displays updates text and hit geometry; failed rebuilding retains usable presentation.
- [ ] **Validation:** Verify display movement on available platforms; run core aggregate tests, relevant rendering checks, and same-cache smoke.
