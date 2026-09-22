# Handle unavailable server paths without process termination

**Severity:** CRITICAL  
**Reported by:** Claude Fable 5.1

`libs/draxul-server/src/server_terminal_runtime.cpp:266` uses throwing `current_path()` for an unspecified cwd. `libs/draxul-server/src/server_kernel.cpp:298` uses throwing `exists()` during restore preparation. A deleted cwd or inaccessible checkpoint path can escape server execution.

**Investigation**

- [x] Exercise terminal startup with a deleted cwd and server startup with an inaccessible checkpoint path.

**Fix strategy**

- [x] Use error-code filesystem operations and explicit failure/fallback policy.
- [x] Preserve startup failure reporting and contain request exceptions with the layout-stream fix.

**Acceptance criteria**

- [x] Filesystem failures produce actionable errors without terminating unrelated sessions.
- [x] Startup failure is reported through the normal discovery/failure channel.

**Validation:** On macOS Debug, the two focused production-path cases passed 29
assertions. `python3 do.py test debug` passed all 23 selected core entries, and
`python3 do.py smoke debug --skip-build` passed from the same cache. The deleted
current-directory fixture is POSIX-specific; the checkpoint inspection seam and
recovery test compile for Windows, but Windows was not run in this environment.
