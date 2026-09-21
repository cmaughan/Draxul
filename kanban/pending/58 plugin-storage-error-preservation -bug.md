# Preserve plugin storage errors during temporary-file cleanup

**Severity:** MEDIUM  
**Reported by:** Claude Fable 5.1

`libs/draxul-host/src/plugin_host.cpp:190` returns a write failure without assigning an I/O error. Cleanup at `:193` and `:199` reuses the primary error code and can clear replacement failures. Hot reload can consequently report a storage failure as “Success.”

**Investigation**

- [ ] Inject open, write, flush, and replacement failures followed by successful cleanup.

**Fix strategy**

- [ ] Capture the original failure before cleanup.
- [ ] Use a separate cleanup error code.

**Acceptance criteria**

- [ ] Every failed operation returns a meaningful non-success diagnostic.
- [ ] Successful cleanup cannot erase the original error.
- [ ] Existing reload rollback behavior remains intact.
