# Stop terminal children crashing between fork and exec on macOS

**Summary:** Start server terminal processes in a way that cannot crash inside macOS's fork handlers, so a new terminal never fails to start because its child process aborted before running the shell.

**Priority:** P2 — three crash reports on 2026-10-07; each lost one terminal start.

**Source:** `libs/draxul-terminal-process/src/unix_pty_process.cpp` (`UnixPtyProcess::spawn`).

macOS crash reports `draxul-server-2026-10-07-{080132,093134,093429}.ips` all abort with "BUG IN CLIENT OF LIBPLATFORM: os_once_t is corrupt" and "crashed on child side of fork pre-exec". The stack is `fork` → `libSystem_atfork_child` → `_notify_fork_child` → `_os_alloc_once`, from `UnixPtyProcess::spawn` ← `ServerTerminalRuntime::start_process` ← `RemoteTerminalService::ensure_runtime_started` during `initialize_session`. Forking a multithreaded process while another thread is inside a libnotify one-time initializer leaves that state corrupt in the child.

- [ ] Confirm which server threads touch libnotify/os_once around Session initialization and whether the parent observes the failed start.
- [ ] Replace `fork`+`exec` with `posix_spawn` (with `POSIX_SPAWN_SETSID` and controlling-terminal setup), or otherwise avoid running libSystem fork handlers, keeping Linux behaviour unchanged.
- [ ] Add a stress test that starts many terminals while other server threads are active, then run the server aggregate and smoke.
