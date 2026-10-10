# Search Kanban cards by their text

**Summary:** Add a search line to the top of the Kanban board that narrows the board, as you type, to cards whose text contains the query, so a card can be found without scanning every lane.

**Priority:** P2 — navigation convenience on large aggregate boards.

- [x] Draw a one-line search box above the column headers without reducing the visible card rows (it replaces the blank spacer row).
- [x] Open it with `/` (keydown or typed text), edit with Backspace, word delete and Ctrl+U, keep the filter with Enter and clear it with Escape; arrows keep browsing while typing.
- [x] Match every whitespace-separated term against card file names and Markdown text on a worker thread, abandon superseded queries, cache file text by modification time and size, and keep the previous filter until the latest results arrive.
- [x] Re-run an active search after external card edits and keep a matched card visible after a lane move.
- [x] Cover typing, editing, navigation hand-back, empty results, external edits and lane moves in Kanban host integration tests; run the core aggregate and same-cache smoke; document the feature.
