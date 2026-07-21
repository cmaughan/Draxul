# Make sessions file-backed only

**Type:** refactor
**Priority:** 26
**Raised by:** User; detailed by GPT/Codex

## Goal

Keep automatic last-state restore and named session save/load while deleting
the live background-owner, attach/detach, tray/Dock reopen, and standalone
attach-picker machinery. The detailed execution plan is
`plans/file-backed-sessions-only.md`.

## Implementation

- [x] Preserve automatic checkpoint/shutdown save and startup restore.
- [x] Preserve command-palette `save_session_as` and `load_session` with rollback.
- [x] Remove live session IPC, protocol, platform transports, and CMake wiring.
- [x] Remove persistent-owner and attach/detach CLI modes and startup retry logic.
- [x] Replace `--kill-session` with file-only `--delete-session`.
- [x] Remove standalone session picker/self-launcher while retaining the in-app picker.
- [x] Remove hidden-window lifecycle, Windows tray, and macOS Dock reopen code.
- [x] Remove runtime liveness metadata and simplify session listings to saved state.
- [x] Delete obsolete tests and retain file-backed state/save/load/close coverage.
- [x] Update `docs/features.md` and test-inventory baselines.

## Validation

- [x] Targeted CLI, session-state, App save/load, and close-lifecycle tests pass.
- [x] Windows Release full build, CTest, smoke, and render snapshots pass.
- [x] Windows Debug full build, CTest, smoke, and render snapshots pass.
- [x] macOS source/build wiring inspected; runtime status recorded.
- [x] No attach, tray, Dock-reopen, hidden-owner, or live-metadata references remain.

### Validation status (2026-07-21)

Windows Debug and Release both built the complete tree and passed all 22 CTest
entries, including app smoke, both session/app shards, and all five render
snapshots. The focused session/app shards also passed after adding direct
window-close persistence coverage. macOS was not executed in this Windows
workspace; its CMake/source wiring was inspected after removing the Dock
reopen implementation, leaving the existing Cocoa title-bar and key-repeat
helpers intact.

<model>GPT-5 Codex</model>
