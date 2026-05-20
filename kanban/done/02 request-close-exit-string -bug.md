# 02 request-close-exit-string -bug

**Priority:** HIGH
**Type:** Bug (fragile close mechanism; fails for fish, sub-processes, custom --command)
**Raised by:** Claude (primary), confirmed by GPT
**Model:** claude-sonnet-4-6

---

## Problem

`TerminalHostBase::request_close()` (referenced at `terminal_host_base.h:42`) sends the literal string `"exit\r"` to the shell to trigger a close. This is fragile:

1. Does not work when the shell has a sub-process running (e.g., `vim` or `python` — the sub-process receives the literal text, not a shell command).
2. Does not work for `fish` in all circumstances.
3. Does not work for a pane started with `--command some_program` where the program does not interpret `exit`.
4. A shell with `set -e` might behave differently.

The correct mechanism is to close the write end of the pipe (sending EOF), which signals the shell/process that stdin is exhausted and it should exit cleanly. If the process is unresponsive after a timeout, send SIGHUP (Unix) or `TerminateProcess` (Windows).

---

## Code Locations

- `libs/draxul-host/include/draxul/terminal_host_base.h:42` — `request_close()` declaration
- `libs/draxul-host/src/terminal_host_base.cpp` — `request_close()` implementation
- `libs/draxul-host/src/shell_host_unix.cpp` — process/pipe management (write pipe handle)
- `libs/draxul-host/src/shell_host_win.cpp` — Windows equivalent
- `libs/draxul-host/src/powershell_host_win.cpp` — PowerShell equivalent

---

## Implementation Plan

- [x] Read `terminal_host_base.cpp` to find the current `request_close()` implementation.
- [x] Read `shell_host_unix.cpp` and `shell_host_win.cpp` to understand how the write pipe is held.
- [x] Add a virtual `do_close()` method to `TerminalHostBase` (or expose a `close_stdin()` hook) that derived classes can override.
- [x] In `shell_host_unix.cpp`: override to close the write end of the stdin pipe (call `close(write_fd_)` or equivalent). The shell will receive EOF and exit cleanly.
- [x] In `shell_host_win.cpp` / `powershell_host_win.cpp`: override to call `CloseHandle(write_pipe_)`.
- [x] If the process is still alive after a short timeout (e.g., 500ms), send SIGHUP on Unix / `TerminateProcess` on Windows as a fallback. Keep the timeout non-blocking (post a deferred check, do not sleep on the main thread).
- [x] Remove the `"exit\r"` write path.
- [x] Build: `cmake --build build --target draxul draxul-tests && py do.py smoke`
- [x] Run `clang-format` on all modified files.

---

## Acceptance Criteria

- Closing a shell pane works when a sub-process (e.g., `cat`) is running.
- Closing a `fish` shell pane works.
- Closing a pane started with `--command python3` works.
- No regression for standard `zsh`/`bash` close.
- Close does not block the UI thread.

---

## Interdependencies

- Touches `shell_host_unix.cpp`, `shell_host_win.cpp`, `powershell_host_win.cpp` simultaneously — a sub-agent doing this should be aware the Windows side may need a different handle type.
- No upstream blockers.

---

*claude-sonnet-4-6*
