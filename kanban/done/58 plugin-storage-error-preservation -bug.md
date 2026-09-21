# Preserve plugin storage errors during temporary-file cleanup

**Severity:** MEDIUM
**Reported by:** Claude Fable 5.1

`libs/draxul-host/src/plugin_host.cpp:190` returns a write failure without assigning an I/O error. Cleanup at `:193` and `:199` reuses the primary error code and can clear replacement failures. Hot reload can consequently report a storage failure as “Success.”

**Investigation**

- [x] Inject open, write, flush, and replacement failures followed by successful cleanup.
- [x] Coordinate `kanban/done/94 plugin-storage-private-owner -refactor.md`: its private filesystem-operation seam now provides deterministic failures, while this bug card retains ownership of primary-error capture and separate cleanup errors.

**Fix strategy**

- [x] Capture the original failure before cleanup.
- [x] Use a separate cleanup error code.

**Acceptance criteria**

- [x] Every failed operation returns a meaningful non-success diagnostic.
- [x] Successful cleanup cannot erase the original error.
- [x] Existing reload rollback behavior remains intact.

**Completion evidence:** The direct `[plugin][storage]` suite passed 99 assertions across four cases, including open, write, flush, and replacement failures followed by successful cleanup. The 39-shard product aggregate and same-cache smoke also passed.
