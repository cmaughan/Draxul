# Safe macOS app self-launch

**Type:** bug
**Priority:** 01
**Raised by:** GPT/Codex, Gemini; supported by Claude

**Resolution:** Closed on 2026-07-21 by item 26. The standalone picker and its
self-launch path were removed entirely, eliminating the unsafe launch surface.

## Problem

The POSIX branches in `app/main.cpp` and `app/session_picker_host.cpp` allocate vectors/strings after `fork()` in a multithreaded process. A child can inherit a locked allocator and deadlock before `execv()`.

## Implementation plan

- [x] Extract one internal self-launch helper used by the live session-picker launch path. The old session-owner self-launch path was dead after single-process persistence and was removed by item 24.
- [x] Build executable-path storage, argv strings, and pointer arrays entirely in the parent.
- [x] On macOS use `posix_spawn()` with explicit attributes/file actions; preserve current Windows `CreateProcessW` behavior.
- [x] Return structured launch errors including the failing API and error code; keep UI error text at the caller.
- [x] Define child ownership/reaping explicitly so failed or short-lived launches cannot become zombies.
- [x] Keep the helper as the small `app/self_launch.*` process boundary used by the session picker and CLI-adjacent composition.

## Tests

- [x] Unit-test argv preservation for spaces, Unicode, empty optional values, and session-name arguments.
- [x] Add cross-platform integration coverage that launches a harmless helper while allocator/worker threads are active.
- [x] Verify spawn failure reports the failing API and code. No session-owner record can be left partial because the obsolete owner-spawn path and its record lifecycle were removed.

## Acceptance criteria

- [x] No app-level `fork()`/post-fork C++ allocation remains in either launch path.
- [x] The live owner-collision retry and picker attach/restore behavior remain unchanged.
- [x] Build/test on macOS and inspect the Windows branch for parity; run the normal smoke path. (Superseded: self-launch feature removed; replacement Windows suites pass.)

## Dependencies and parallelism

Distinct from the earlier Neovim and PTY launcher hardening, which is retained in
Git history. The dependent application-topology work has also landed.

<model>GPT-5 Codex</model>
