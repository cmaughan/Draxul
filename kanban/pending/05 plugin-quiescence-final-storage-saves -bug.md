# Keep valid storage callbacks available during plugin quiescence
**Severity:** HIGH  
**Source:** Codex #6; `libs/draxul-host/src/plugin_host.cpp:224`.

Shutdown and reload mark the host shutting down before quiescence, causing final storage saves—such as SatView preferences—to fail.

**Investigation**

- [ ] Trace callback validity, generation checks, and final saves through close, shutdown, and reload.

**Fix strategy**

- [ ] Permit legitimate main-thread storage calls during quiescence.
- [ ] Retire callbacks afterward while continuing to reject late asynchronous and stale-generation calls.

**Acceptance criteria**

- [ ] SatView panel preferences survive close/reopen and reload.
- [ ] Callback retirement remains safe; run shared plugin aggregate coverage and same-cache smoke.
- [ ] Coordinate with `01 plugin-storage-exclusive-temporary-files -bug.md`.
