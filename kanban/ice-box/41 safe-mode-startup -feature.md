# Safe-mode startup and recovery

**Type:** feature
**Priority:** 41
**Raised by:** GPT/Codex

## User need

When config, fonts, modules, session state, or network services break startup, users need a deterministic way to open Draxul with minimal defaults and diagnostics.

## Implementation plan

- [ ] Add `--safe-mode` to `CliArgs`, help, tests, and docs.
- [ ] Define safe-mode policy: default config/theme/font, fresh shell workspace, optional modules not auto-launched, network services disabled, saved session not mutated, diagnostics/log file enabled.
- [ ] Detect repeated/previous startup failure with a small atomic marker cleared after successful initialization.
- [ ] On the next normal launch offer Retry, Start Safe Mode, or Open Config/Log without entering a restart loop.
- [ ] Keep user config/session files read-only in safe mode unless the user explicitly chooses a repair action.
- [ ] Add a diagnostics banner listing bypassed components and paths.
- [ ] Ensure command-line explicit log/host choices interact predictably with safe mode.

## Tests and acceptance

- [ ] Cover malformed config, bad font, corrupt session, unavailable module asset, failed renderer, and failure-marker lifecycle.
- [ ] Prove safe mode opens without network and does not overwrite normal config/session state.
- [ ] Test CLI precedence and recovery-loop prevention.
- [ ] Build/tests/smoke pass on Windows and macOS paths.

## Dependencies and parallelism

Benefits from items 07/21 and 45. Can be designed in parallel, but implementation touches startup/App and should not overlap item 22/24.

<model>GPT-5 Codex</model>
