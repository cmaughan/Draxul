# Move Windows Neovim process waits off the UI thread

**Severity:** HIGH  
**Reported by:** Claude Fable 5.1

`libs/draxul-nvim/src/nvim_process.cpp:233` and `:237` perform consecutive two-second waits. `libs/draxul-host/src/nvim_host.cpp:182` calls shutdown synchronously, so closing an unresponsive Neovim pane can freeze the interface.

**Investigation**

- [ ] Trace process, RPC reader, and pipe ownership during Windows pane closure.

**Fix strategy**

- [ ] Transfer bounded process termination/reaping to a safe background owner.
- [ ] Preserve cancellation, handle lifetime, and idempotent shutdown.

**Acceptance criteria**

- [ ] Closing an unresponsive Neovim pane leaves other panes responsive.
- [ ] The child is eventually reaped without leaked handles or worker access to destroyed state.
- [ ] Preserve the existing nonblocking POSIX behavior.
