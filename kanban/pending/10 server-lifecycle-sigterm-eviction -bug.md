# Give the server a survivable lifecycle: SIGTERM and endpoint eviction

**Type:** bug
**Priority:** P1 / sequence 10
**Raised by:** Claude (first macOS validation of codex/server-client-runtime, 2026-08-03)

## Goal

A Draxul server that should die, doesn't. Two gaps observed live on macOS
(three tray-icon servers accumulated during validation, one reachable):

1. **SIGTERM is ignored.** A healthy foreground server survived `kill -TERM`
   indefinitely (retested deliberately: still running 15s later; only
   SIGKILL worked). `kill`, logout, launchd, and any supervisor therefore
   cannot stop a server. This may interact with the deliberate stop-
   confirmation flow ("Unify server stop confirmation flow",
   "Prevent modal tray shutdown zombies") — the design question is whether
   SIGTERM should bypass live-terminal confirmation. Recommended: SIGTERM →
   graceful `request_stop()` (checkpoint sessions, then exit); reserve the
   confirmation dialog for interactive stops. Document whatever is decided.
2. **No endpoint-eviction self-check.** A server whose endpoint was removed
   or replaced (crash cleanup, a wiped runtime dir, a newer server binding
   the same path) keeps running forever: invisible to clients, unreachable
   by the CLI, tray icon its only surface. The accumulation recipe is then
   one zombie per relaunch. The client side already verifies
   `metadata_process_matches` via pid + process-start-token; the server
   never checks its own publication. Recommended: the kernel periodically
   verifies it is still the published server (metadata pid/start-token ==
   self) and retires gracefully when evicted.

## Repro (observed 2026-08-03)

- Start `draxul --server ...`; `kill <pid>` → survives.
- Delete the runtime dir under a live server → it keeps running with no
  endpoint; next `draxul` launch correctly spawns a fresh server → two
  servers, two tray icons, one reachable. Repeat → three.

## Implementation

- [ ] Decide and record the SIGTERM policy (see design question above).
- [ ] Install SIGTERM/SIGINT handlers in `--server` mode that route to
      `ServerKernel::request_stop()`; verify checkpoint-on-signal.
- [ ] Add the eviction self-check (bounded interval, e.g. every 5-10s):
      metadata missing, or pid/start-token not self → graceful retire with a
      log line naming the reason.
- [ ] Windows parity: console control handler / equivalent, and confirm the
      eviction check reads the pipe-world metadata the same way.

## Acceptance

- [ ] `kill -TERM <server>` exits cleanly within the checkpoint budget, on
      both platforms' equivalents.
- [ ] A server whose runtime dir is wiped retires itself within the check
      interval; no tray icon remains.
- [ ] Two-server accumulation is no longer reproducible via the recipe
      above.

## Dependencies and parallelism

Independent of `kanban/pending/09 macos-remote-terminal-channel -bug.md`;
touches `libs/draxul-server/src/server_kernel.cpp` and the `--server` entry
in `app/main.cpp`. Coordinate the SIGTERM decision with whoever owns the
stop-confirmation UX.
