# Propagate Kanban scan errors without replacing the board

**Severity:** CRITICAL
**Reported by:** Claude Fable 5.1

`modules/kanban/draxul-kanban/src/kanban_store.cpp:289` and `:315` check iterator-construction errors only inside loops that never run on failure. Their range-for increments can also throw. Permission loss or filesystem failure during reload can hide a board or escape the UI callback.

**Investigation**

- [x] Inject iterator construction and advancement failures for roots and columns.

**Fix strategy**

- [x] Check construction errors immediately and advance through `increment(error_code)`.
- [x] Return scan failures before committing a replacement board.

**Acceptance criteria**

- [x] Failed reloads preserve the last valid board and expose an error.
- [x] Mid-scan failures never escape the UI callback.

**Completion evidence**

- Production root and column scans now share a private cursor seam that preserves
  immediate construction checks and explicit `increment(error_code)` handling.
- Focused store coverage passed 20 assertions across root/column construction and
  advancement failures; focused host reload coverage passed 22 assertions proving
  that failures do not escape, the prior board remains visible, and a later
  successful reload clears the error and publishes the replacement board.
- The core aggregate built the full current tree (including the concurrent SDL
  event-registration slice) and passed 22 of 23 CTest entries. Its only failure was
  the concurrently added inaccessible-checkpoint diagnostic case, outside this
  card; both Kanban entries passed.
- Same-cache macOS Metal smoke passed on Apple M5.
