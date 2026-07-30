# Default shared server and status surface

## Outcome

Ordinary Draxul shell launches use the shared server by default, while standalone
product hosts and `--no-server` remain independent.

## Acceptance

- [x] A normal shell launch discovers or starts the per-user server.
- [x] Closing every UI leaves server-owned terminals and topology alive.
- [x] Cleanly detached and stale clients disappear from the status count.
- [x] Windows notification-area and macOS menu-bar code expose status, Open Draxul,
      Open Log, graceful stop, and guarded force stop.
- [x] Graceful stop refuses live terminals without explicit confirmation.
- [x] Incompatible servers are reported and left running.
- [x] Server status/log/stop actions are available from the command palette.
- [x] `--list-sessions` reports the running server's Session registry instead of
      legacy client-side saved Session files.
- [x] `--delete-session --session <id>` deletes a detached server Session and
      checkpoint, refusing attached UIs and requiring confirmation for live terminals.
- [x] `--no-server` and explicit standalone product-host startup stay client-owned.
- [x] Focused parser, app, server-kernel, and isolated executable gates pass.

## Validation

The repeatable two-window and detach/reconnect procedure is recorded under Slice 9
in `plans/server-client-terminal-runtime.md`. Windows is validated locally; the same
menu-bar and lifecycle path remains part of the macOS CI/manual gate.
