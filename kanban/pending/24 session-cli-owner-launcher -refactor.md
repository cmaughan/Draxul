# Extract session CLI and owner launcher

**Type:** refactor
**Priority:** 24
**Raised by:** GPT/Codex, Claude

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
- [ ] CLI/session tests, full build, `ctest`, and smoke pass.

### Validation status (2026-07-21)

Windows Release build-only coverage is green: `draxul-test-app` compiled and
linked `session_cli_tests.cpp` and `self_launch_tests.cpp`, and a Release
`draxul` build linked the extracted CLI/launcher path successfully. No test,
helper process, or application executable was launched. This card remains
pending for runtime CLI/session integration, `ctest`, smoke, and macOS launch
execution.

## Dependencies and parallelism

Depends on the app self-launch fix (`ice-box/01 macos-app-self-launch -bug.md`, currently deferred); should land before the SessionController extraction (`ice-box/22 app-workspace-session-controllers -refactor.md`, also deferred). One session/process sub-agent should own `main.cpp` during this work.

<model>GPT-5 Codex</model>
