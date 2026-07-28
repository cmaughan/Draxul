# Remote terminal takeover closes observer

## Problem

When two `--experimental-remote-terminal` windows are attached, sustained input can
make both appear to crash. The controller issued one named-pipe RPC per text event
and drained its command backlog before polling. Under load, that starved both clients
until their remote-host workers stopped; the application then cleanly closed its
only dead pane.

## Acceptance

- [x] Two rendered remote-terminal hosts can repeatedly transfer control.
- [x] The former controller remains running and renders the new shared dimensions.
- [x] Expected ownership races and transient local transport contention do not kill
      either host.
- [x] Adjacent input is batched and command work is bounded between projection polls.
- [x] Two optimized Release Vulkan windows survive the previously failing
      3,600-character burst.
- [x] A fatal protocol or server-identity error still stops the affected host.
- [x] Release build, focused regression, full CTest, and smoke pass.

## Rollback

The experimental remote-terminal CLI remains opt-in; the fix must not change local
terminal or Session behavior.
