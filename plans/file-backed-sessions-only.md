# File-backed sessions only

**Status:** superseded by
[`server-client-terminal-runtime.md`](server-client-terminal-runtime.md).

This document records the short-lived client-owned design before the shared
server became authoritative. Its local shell backend, named save/load commands,
and file-backed CLI are no longer production behavior.

## Outcome

Draxul keeps automatic last-state restore and named session save/load, but the
application process lives exactly as long as its window. Closing the final
window checkpoints the current file-backed session and exits. No background
owner, live attach endpoint, tray icon, Dock reopen handler, or attach-oriented
helper process remains.

## Preserved behavior

- Normal desktop launches restore the selected saved shell-session topology.
- Periodic checkpoints and clean shutdown persist tabs, split trees,
  focus, pane/tab names, launch commands, and working directories.
- `save_session_as` and `load_session` remain available through the command
  palette and retain rollback on failed load/save transitions.
- `--session`, `--new-session`, `--session-name`, and saved-session listing,
  rename, and deletion remain file-backed operations.
- Explicit `--host` launches continue to override saved shell topology.

## Removed behavior

- Live session ownership, probing, activate/detach/shutdown/rename IPC, and
  platform named-pipe/Unix-socket transports.
- `--persistent-app`, legacy `--session-owner`, `--attach-session`, and
  `--detach-session`.
- Hidden-window event-loop states, background owner collision/retry logic, and
  the Windows tray/macOS Dock reopen paths.
- The standalone `--pick-session` host and its cross-process self-launcher; the
  in-window saved-session picker remains canonical.
- Runtime `.meta.toml` liveness files and live/detached/PID/timestamp summary
  state. Existing legacy metadata files are ignored.
- `--kill-session`; saved-state deletion is exposed as `--delete-session`.

## Implementation sequence

1. Simplify session data and CLI interfaces to be file-backed only.
2. Remove `SessionAttachServer`, its transport/protocol/platform sources, and
   the standalone picker/self-launch sources from CMake.
3. Remove attach/detach state from `App`; make every close path save and exit.
4. Remove tray/Dock reopen callbacks and visibility APIs used only by detached
   sessions while preserving render-test startup hiding.
5. Delete feature-specific tests and retain or relocate checkpoint, save/load,
   topology, rollback, and clean-close coverage.
6. Update the feature inventory and run targeted tests followed by the full
   Windows Release and Debug suites.

## Validation contract

- Session-state serialization round trips unchanged topology and names.
- Startup restores the last saved session and explicit host selection still
  bypasses restore.
- Named save/load succeeds and load rollback preserves the original session.
- Closing the window never hides Draxul or leaves a background process.
- No attach endpoint, tray icon, Dock reopen callback, or live metadata is
  built or referenced.
- Full build, CTest, smoke, and render snapshots pass on Windows; macOS source
  and build wiring remain valid pending execution on macOS.
