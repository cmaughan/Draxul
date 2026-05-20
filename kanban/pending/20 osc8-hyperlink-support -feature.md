# Feature: OSC 8 Hyperlink Support

**Type:** feature
**Priority:** 20
**Source:** Gemini review

## Overview

OSC 8 is the standard terminal protocol for clickable hyperlinks:

```
ESC ] 8 ; params ; uri ST  ← start hyperlink
... text content ...
ESC ] 8 ;; ST              ← end hyperlink
```

Applications (like `ls --hyperlink`, `git log`, man pages, etc.) emit OSC 8 to mark text as clickable links pointing to a URI. The user can then click the text in the terminal to open the URI in the system browser.

This is complementary to the icebox item `20 url-detection-click` (heuristic URL detection via regex). OSC 8 is the precise, application-controlled version and should be implemented first because it is more correct (no false positives).

## Implementation plan

### Phase 1: Parse OSC 8 sequences

- [ ] Read `libs/draxul-host/src/terminal_host_base_osc.cpp` (or wherever OSC parsing happens) — find the OSC dispatch table.
- [ ] Add a handler for OSC sequence `8`: parse `params` (a semicolon-separated list, typically empty) and `uri`.
- [ ] When OSC 8 with a non-empty URI is received, set a "current hyperlink" in the terminal host state.
- [ ] When OSC 8 with an empty URI is received, clear the current hyperlink.

### Phase 2: Store URIs in cell metadata

- [ ] Decide on storage: options include:
  1. A per-cell `uint16_t link_id` field (zero = no link), with a separate `LinkTable` that maps IDs to URI strings. (Preferred — small per-cell cost, reuses the attr-ID pattern.)
  2. A per-cell `std::optional<std::string>` (expensive per cell).
- [ ] If using link IDs: add `link_id` to the `Cell` struct in `grid.h` and a `LinkTable` alongside `HighlightTable`.
- [ ] During `grid_line` handling, set `link_id` on cells that are within an OSC 8 region.

### Phase 3: Mouse interaction

- [ ] On `SDL_MOUSEMOTION` over a cell with a non-zero `link_id`: show the URI in the status bar or a tooltip.
- [ ] On `SDL_MOUSEBUTTONUP` (left click) over a linked cell: open the URI with `open` (macOS) / `ShellExecute` (Windows).
- [ ] Ensure the click is not forwarded to Neovim when it is consumed by a hyperlink.

### Phase 4: Visual indication

- [ ] Optionally: underline cells with `link_id != 0` (if not already underlined by the highlight attr).
- [ ] Add a config option `enable_osc8_hyperlinks = true` to allow users to disable if it conflicts with workflow.

## Acceptance criteria

- [ ] `echo -e "\033]8;;https://example.com\033\\click me\033]8;;\033\\"` in a terminal pane renders the text and opens the browser on click.
- [ ] OSC 8 within Neovim (e.g. from a plugin that emits hyperlinks) also works.
- [ ] URI tooltip/status bar is shown on hover.
- [ ] Cells without OSC 8 are unaffected.

## Interdependencies

- **Icebox `20 url-detection-click`**: OSC 8 is the explicit protocol; url-detection-click adds heuristic detection on top. OSC 8 first.
- **`15 attribute-cache-shared-class -refactor`**: the link-ID table follows the same pattern as the attr-ID cache; review that refactor for reuse.
- **`24 osc133-shell-integration -feature`**: both parse OSC sequences; share OSC dispatch infrastructure.

---
*Filed by `claude-sonnet-4-6` · 2026-03-26*
