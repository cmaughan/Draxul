# Extract session CLI and owner launcher

**Type:** refactor
**Priority:** 24
**Raised by:** GPT/Codex, Claude

**Resolution:** Superseded on 2026-07-21 by item 26, which removed live
session ownership, self-launch, attach/detach, and the standalone picker. The
remaining saved-session CLI boundary is retained and tested.

## Goal

Move attach/list/kill/rename modes, owner launch/retry, and self-launch mechanics out of the 940-line `app/main.cpp` while keeping `main` as composition and dispatch.

## Implementation plan

- [x] Implement the app self-launch fix (`ice-box/01 macos-app-self-launch -bug.md`) so the live picker launcher uses safe Windows/POSIX behavior.
- [x] Define a `SessionCliRequest` from `CliArgs` and a result enum distinguishing handled/continue/error.
- [x] Remove the dead owner-argument/spawn/attach path. Single-process persistence no longer called it, so retaining a `SessionOwnerLauncher` abstraction would preserve unreachable behavior. The live collision retry/backoff now belongs to `SessionCli`.
- [x] Extract list/attach/detach/kill/rename orchestration into `SessionCli` using injected state/attach services.
- [x] Keep console/window setup, provider registration, and final App construction in `main.cpp`.
- [x] Make self-launch testable without spawning the Draxul GUI via an injected picker process API and a structured launch command/result.
- [x] Preserve exit codes and all existing CLI text unless a documented bug requires change.

## Tests and acceptance

- [x] Table-test every live session CLI mode, invalid combination, retry result, and self-launch argument (the old owner argument is dead and removed).
- [x] Add Windows/macOS launch integration coverage using a harmless platform helper process.
- [x] `draxul_main()` contains no session protocol or platform process implementation.
- [x] CLI/session tests, full build, `ctest`, and smoke pass. macOS validated
  before removal (session CLI suite, CTest 22/22, and smoke green on
  2026-07-21); the retained file-backed behavior is validated by item 26.

### Validation status (2026-07-21)

Windows Release automated coverage is green: `t.bat release` built the full
tree and passed all 22 CTest entries, including the CLI/session cases, harmless
helper-process launch coverage, and app smoke. The exact session CLI and
self-launch tests also passed as part of the app shards. The user completed a PC
visual check with no behavior regression observed. Debug built and passed smoke,
but the aggregate Debug suite is not fully green because the separate session
attach owner/DACL assertion recorded on item 25 fails deterministically. This
card remains pending for macOS launch/runtime execution before the platform
acceptance gate is closed.

## Dependencies and parallelism

Depends on the app self-launch fix (`ice-box/01 macos-app-self-launch -bug.md`, currently deferred); should land before the SessionController extraction (`ice-box/22 app-tab-session-controllers -refactor.md`, also deferred). One session/process sub-agent should own `main.cpp` during this work.

<model>GPT-5 Codex</model>

## Verified 2026-07-21

SessionCli + SessionOwnerLauncher extracted (app/session_cli.*) with session_cli_tests. Verified on the current TOT: build clean, ctest 22/22 (incl. session_cli), smoke green. Moved to done.
