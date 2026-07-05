# Safe macOS app self-launch

**Type:** bug
**Priority:** 01
**Raised by:** GPT/Codex, Gemini; supported by Claude

## Problem

The POSIX branches in `app/main.cpp` and `app/session_picker_host.cpp` allocate vectors/strings after `fork()` in a multithreaded process. A child can inherit a locked allocator and deadlock before `execv()`.

## Implementation plan

- [ ] Extract one internal self-launch helper used by the session owner and session picker.
- [ ] Build executable-path storage, argv strings, and pointer arrays entirely in the parent.
- [ ] On macOS use `posix_spawn()` with explicit attributes/file actions; preserve current Windows `CreateProcessW` behavior.
- [ ] Return structured launch errors including the failing API and error code; keep UI error text at the caller.
- [ ] Define child ownership/reaping explicitly so failed or short-lived launches cannot become zombies.
- [ ] Keep the helper small enough to move into the session CLI component in item 24 without changing behavior again.

## Tests

- [ ] Unit-test argv preservation for spaces, Unicode, empty optional values, and session-name arguments.
- [ ] Add a macOS integration test that launches a harmless helper while allocator/worker threads are active.
- [ ] Verify spawn failure is reported without leaving a partial session owner record.

## Acceptance criteria

- [ ] No app-level `fork()`/post-fork C++ allocation remains in either launch path.
- [ ] Session-owner retry and picker attach/restore behavior remain unchanged.
- [ ] Build/test on macOS and inspect the Windows branch for parity; run the normal smoke path.

## Dependencies and parallelism

Distinct from completed `kanban/done/04 fork-child-async-signal-unsafe -bug.md`, which covered Neovim and PTY launchers. Blocks item 24. Suitable for one session-focused agent.

<model>GPT-5 Codex</model>
