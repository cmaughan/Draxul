# Singleton server bootstrap

## Goal

Implement Slice 2 of `plans/server-client-terminal-runtime.md`: a real, authenticated,
per-user Draxul server identity that clients can discover or launch, without moving
terminal ownership out of the UI yet.

## Acceptance

- [x] `draxul --server` dispatches before SDL, renderer, Neovim, or product setup.
- [x] One authenticated endpoint publishes PID, epoch, protocol, build, readiness,
      capabilities, and status.
- [x] `--server-status`, `--shutdown-server`, and `--server-runtime-dir` work.
- [x] `--experimental-server-client` discovers or detached-launches the singleton.
- [x] Concurrent launches converge on one live PID and epoch.
- [x] Diagnostics show the connected server identity and clearly say terminals remain
      local.
- [x] Absent, starting, ready, stale, busy, incompatible, crashed, and shutdown paths
      have automated coverage.
- [x] Explicit standalone hosts and smoke/render paths do not bootstrap the server.
- [x] New protocol/client/server targets preserve the renderer-free dependency
      boundary.
- [x] `draxul`, `draxul-tests`, full CTest, and smoke validation pass.

## Rollback

Ordinary startup remains unchanged. Removing `--experimental-server-client` keeps the
existing local terminal path.
