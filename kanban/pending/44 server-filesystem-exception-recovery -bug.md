# Handle unavailable server paths without process termination

**Severity:** CRITICAL  
**Reported by:** Claude Fable 5.1

`libs/draxul-server/src/server_terminal_runtime.cpp:266` uses throwing `current_path()` for an unspecified cwd. `libs/draxul-server/src/server_kernel.cpp:298` uses throwing `exists()` during restore preparation. A deleted cwd or inaccessible checkpoint path can escape server execution.

**Investigation**

- [ ] Exercise terminal startup with a deleted cwd and server startup with an inaccessible checkpoint path.

**Fix strategy**

- [ ] Use error-code filesystem operations and explicit failure/fallback policy.
- [ ] Preserve startup failure reporting and contain request exceptions with the layout-stream fix.

**Acceptance criteria**

- [ ] Filesystem failures produce actionable errors without terminating unrelated sessions.
- [ ] Startup failure is reported through the normal discovery/failure channel.
