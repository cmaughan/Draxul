# Preserve unknown ConPTY process status

**Severity:** MEDIUM  
**Type:** Bug

## Bug description

`ConPtyProcess::is_running()` ignores `GetExitCodeProcess` failure, caches the initialized value zero, and permits the server to treat an unknown status as a clean exit.

**Trigger:** `GetExitCodeProcess` fails transiently while the shared server evaluates a started terminal.

## Investigation

- [x] Add an injectable process-status seam covering active, exited, and query-failed states.
- [x] Trace all callers that currently interpret `false` or a missing exit code as clean exit.
- [x] Verify status checks cannot race process-handle retirement.

## Fix strategy

- [x] Do not update `last_exit_code_` when the Windows query fails.
- [x] Represent query failure as conservatively running.
- [x] Require a confirmed zero exit before automatic topology cleanup.

## Acceptance criteria

- [x] Query failure cannot close or remove a live terminal.
- [x] Confirmed zero exits still receive normal cleanup.
- [x] Nonzero and unknown exits remain available for inspection or recovery.
- [x] Server lifecycle and Windows terminal-process tests pass.

## Windows closeout (2026-09-22)

The injectable active/exited/query-failed matrix and server lifecycle coverage
passed in the final 48/48 products aggregate; same-cache smoke passed.
