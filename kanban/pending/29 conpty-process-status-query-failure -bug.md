# Preserve unknown ConPTY process status

**Severity:** MEDIUM  
**Type:** Bug

## Bug description

`ConPtyProcess::is_running()` ignores `GetExitCodeProcess` failure, caches the initialized value zero, and permits the server to treat an unknown status as a clean exit.

**Trigger:** `GetExitCodeProcess` fails transiently while the shared server evaluates a started terminal.

## Investigation

- [ ] Add an injectable process-status seam covering active, exited, and query-failed states.
- [ ] Trace all callers that currently interpret `false` or a missing exit code as clean exit.
- [ ] Verify status checks cannot race process-handle retirement.

## Fix strategy

- [ ] Do not update `last_exit_code_` when the Windows query fails.
- [ ] Represent query failure as unknown or conservatively running.
- [ ] Require a confirmed zero exit before automatic topology cleanup.

## Acceptance criteria

- [ ] Query failure cannot close or remove a live terminal.
- [ ] Confirmed zero exits still receive normal cleanup.
- [ ] Nonzero and unknown exits remain available for inspection or recovery.
- [ ] Server lifecycle and Windows terminal-process tests pass.
