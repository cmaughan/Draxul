# Safe-mode startup and recovery

**Type:** feature
**Priority:** 41
**Raised by:** GPT/Codex

## User need

When config, fonts, modules, session state, or network services break startup, users need a deterministic way to open Draxul with minimal defaults and diagnostics.

## Implementation plan

- [ ] Add `--safe-mode` to `CliArgs`, help, tests, and docs.
- [ ] Define safe-mode policy: default config/theme/font, isolated server Session
  or explicit diagnostic host, optional modules not auto-launched, remote
  transports disabled, authoritative checkpoints not mutated, and diagnostics/log enabled.
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

The result/config foundations are delivered. Coordinate UX with
`kanban/ice-box/45 first-run-health-center -feature.md`; implementation touches startup/App.

<model>GPT-5 Codex</model>
