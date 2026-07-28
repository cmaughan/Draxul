# Server-owned shell runtime

## Outcome

Add an opt-in real shell whose process, PTY/ConPTY, terminal state, and lifecycle
belong to the singleton Draxul server. Multiple UIs can attach to it, detach, and
later recover the same process and terminal generation.

## Acceptance

- [x] PTY/ConPTY process adapters live below the UI host layer and remain shared by
      local and server-owned terminals.
- [x] `--experimental-remote-shell` lazily creates one server-owned PowerShell on
      Windows or Bash/Zsh shell on macOS.
- [x] Two clients render and control the same real shell through the existing
      controller lease.
- [x] Input, resize, title, cwd, cursor, modes, and process lifecycle originate in
      the server runtime.
- [x] Closing every UI leaves the shell running and draining output.
- [x] Reconnecting returns the same process ID, terminal ID, generation, output,
      and bounded current terminal state.
- [x] Restarting an exited shell changes its runtime generation without changing
      the server epoch.
- [x] Local terminals and the fake remote-terminal diagnostic path remain unchanged.
- [x] Windows Release build, focused process/runtime tests, full CTest, smoke, and a
      real two-window detach/reconnect demonstration pass.
- [x] macOS source/build wiring remains valid through the shared Unix PTY path;
      macOS runtime execution was not available on the Windows validation host.

## Rollback

The real server shell remains behind `--experimental-remote-shell`; removing that
route restores the prior fake-only server/client behavior without changing local
terminal creation.
