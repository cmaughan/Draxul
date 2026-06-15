# Markdown Pipe Tables Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Render GitHub/Obsidian pipe tables in the native Draxul Markdown viewer.

**Architecture:** Reuse MD4C table blocks and add table metadata to the Markdown document model. Convert table blocks into variable-height layout rows with cell decorations and aligned text runs, then let the existing GPU draw-list path render rectangles and glyphs.

**Tech Stack:** C++20, MD4C, `draxul-markdown`, `draxul-font`, Catch2, Vulkan/Metal draw-list render passes.

---

### Task 1: Parser Table Shape

**Files:**
- Modify: `libs/draxul-markdown/include/draxul/markdown/markdown_document.h`
- Modify: `libs/draxul-markdown/src/markdown_parser_md4c.cpp`
- Test: `tests/markdown_parser_tests.cpp`

- [ ] Add `TableCellAlignment` and store alignment plus header/body row flags on `Block`.
- [ ] Add a failing parser test for a pipe table with left, center, and right aligned columns.
- [ ] Map `MD_BLOCK_TABLE_DETAIL`, `MD_BLOCK_TD_DETAIL`, `MD_BLOCK_THEAD`, and `MD_BLOCK_TBODY` into the document model.
- [ ] Run `draxul-tests.exe "[markdown][parser]"`.

### Task 2: Table Layout

**Files:**
- Modify: `libs/draxul-markdown/include/draxul/markdown/markdown_layout.h`
- Modify: `libs/draxul-markdown/src/markdown_layout.cpp`
- Test: `tests/markdown_layout_tests.cpp`

- [ ] Add table decoration kinds for cell borders and row backgrounds using existing rect rendering.
- [ ] Add table layout tests for aligned columns, wrapped cells, header backgrounds, and body row fill.
- [ ] Implement column width calculation, per-cell wrapping, alignment offsets, and row-height calculation.
- [ ] Run `draxul-tests.exe "[markdown][layout]"`.

### Task 3: Draw List And Docs

**Files:**
- Modify: `libs/draxul-markdown/src/markdown_draw_list.cpp`
- Modify: `docs/features.md`
- Test: `tests/markdown_draw_list_tests.cpp`

- [ ] Add a draw-list test that table decorations emit rect instances.
- [ ] Reuse existing decoration-to-rect conversion for table backgrounds and borders.
- [ ] Update feature docs to mention pipe table rendering.
- [ ] Run focused markdown tests, app smoke with a table document, CTest, `python do.py smoke`, and `git diff --check`.
