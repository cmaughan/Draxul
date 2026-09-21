# Propagate Kanban scan errors without replacing the board

**Severity:** CRITICAL  
**Reported by:** Claude Fable 5.1

`modules/kanban/draxul-kanban/src/kanban_store.cpp:289` and `:315` check iterator-construction errors only inside loops that never run on failure. Their range-for increments can also throw. Permission loss or filesystem failure during reload can hide a board or escape the UI callback.

**Investigation**

- [ ] Inject iterator construction and advancement failures for roots and columns.

**Fix strategy**

- [x] Check construction errors immediately and advance through `increment(error_code)`.
- [x] Return scan failures before committing a replacement board.

**Acceptance criteria**

- [x] Failed reloads preserve the last valid board and expose an error.
- [x] Mid-scan failures never escape the UI callback.
