# Draw the Markdown scrollbar without copying the document

**Summary:** Draw the Markdown scrollbar separately from the document so scrolling a long file does not copy and sort all its offscreen content.

**Source:** `modules/markdown/draxul-markdown/src/markdown_host.cpp`  
**Priority/evidence:** P2; static, high confidence. **Reported by:** Claude, Codex. Lines 432–449 call `document_with_scrollbar()`; lines 555–587 copy the full layout and sort decorations before visible-row culling. Wheel or drag scrolling thus copies offscreen rows, text, and decorations.

- [ ] **Baseline:** Measure allocations, copied rows, and scroll CPU at fixed viewport for 1,000 and 100,000 layout rows.
- [ ] **Implement:** Read the retained layout and append separately clipped scrollbar draw commands in the proper paint order.
- [ ] **Functional safety:** Check mixed-height rows, tables, edge scrolling, decoration order, and scrollbar interaction.
- [ ] **Compare:** Require zero complete-document copies per scroll and report latency before/after.
- [ ] **Platforms:** Check Markdown output on Vulkan/Metal, aggregate and smoke.
- [ ] **Acceptance:** Scroll work no longer scales with offscreen document size through copying and sorting.
