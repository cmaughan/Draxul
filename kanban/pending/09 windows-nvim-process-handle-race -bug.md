# Synchronize the Windows Neovim process handle

**Type:** bug
**Priority:** 09
**Raised by:** Claude

## Problem

On Windows, `NvimProcess::is_running()` calls `GetExitCodeProcess` on `proc_info_.hProcess` while `shutdown()` can close that handle. `started_` does not protect the interval between checking the flag and using the handle.

## Implementation plan

- [ ] Replace the shared `PROCESS_INFORMATION` lifetime with an explicitly synchronized process-handle owner.
- [ ] Use a small mutex/guard around process-handle load/use/close, or duplicate the handle before releasing the guard; do not rely on atomic `started_` alone.
- [ ] Clear the published process identity before closing and make repeated shutdown idempotent.
- [ ] Audit Windows `read()`, `write()`, PID logging, and startup failure cleanup for the same ownership contract.
- [ ] Keep stdin/stdout atomics and the POSIX PID path unchanged unless a common abstraction simplifies both without weakening them.

## Tests

- [ ] Add a Windows-only threaded regression test hammering `is_running()` while another thread calls shutdown.
- [ ] Repeat spawn/shutdown cycles and verify no invalid-handle diagnostics or leaks.
- [ ] Cover failed `CreateProcessW` and already-exited processes.

## Acceptance criteria

- [ ] No thread can use the process handle after it is closed.
- [ ] Shutdown remains bounded and idempotent.
- [ ] Windows focused tests, full `ctest`, and smoke pass.

## Dependencies and parallelism

Independent Windows lane. Existing shutdown tests should be extended rather than duplicated.

<model>GPT-5 Codex</model>
