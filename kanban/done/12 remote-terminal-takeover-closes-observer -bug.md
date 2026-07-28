# Remote terminal takeover closes observer

## Problem

When a second `--experimental-remote-terminal` window takes control, the first
window can appear to crash. A remote-host worker failure marks the only pane dead,
which causes the application to close.

## Acceptance

- [x] Two rendered remote-terminal hosts can repeatedly transfer control.
- [x] The former controller remains running and renders the new shared dimensions.
- [x] Expected ownership races and transient local transport contention do not kill
      either host.
- [x] A fatal protocol or server-identity error still stops the affected host.
- [x] Release build, focused regression, full CTest, and smoke pass.

## Rollback

The experimental remote-terminal CLI remains opt-in; the fix must not change local
terminal or Session behavior.
