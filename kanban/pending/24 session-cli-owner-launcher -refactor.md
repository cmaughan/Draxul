# Extract session CLI and owner launcher

**Type:** refactor
**Priority:** 24
**Raised by:** GPT/Codex, Claude

## Goal

Move attach/list/kill/rename modes, owner launch/retry, and self-launch mechanics out of the 940-line `app/main.cpp` while keeping `main` as composition and dispatch.

## Implementation plan

- [ ] Land item 01 so the extracted launcher begins with safe Windows/POSIX behavior.
- [ ] Define a `SessionCliRequest` from `CliArgs` and a result enum distinguishing handled/continue/error.
- [ ] Extract owner-argument construction, launch, retry/backoff, and error formatting into `SessionOwnerLauncher`.
- [ ] Extract list/attach/detach/kill/rename orchestration into `SessionCli` using injected state/attach services.
- [ ] Keep console/window setup, provider registration, and final App construction in `main.cpp`.
- [ ] Make the launcher testable without spawning the Draxul GUI via injected process APIs.
- [ ] Preserve exit codes and all existing CLI text unless a documented bug requires change.

## Tests and acceptance

- [ ] Table-test every session CLI mode, invalid combination, retry result, and owner argument.
- [ ] Add Windows/macOS launch integration coverage using a helper process.
- [ ] `draxul_main()` contains no session protocol or platform process implementation.
- [ ] CLI/session tests, full build, `ctest`, and smoke pass.

## Dependencies and parallelism

Depends on item 01; should land before SessionController extraction in item 22. One session/process sub-agent should own `main.cpp` during this work.

<model>GPT-5 Codex</model>
