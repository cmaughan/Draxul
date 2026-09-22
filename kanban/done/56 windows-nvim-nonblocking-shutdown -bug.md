# Move Windows Neovim process waits off the UI thread

**Severity:** HIGH  
**Reported by:** Claude Fable 5.1

`libs/draxul-nvim/src/nvim_process.cpp:233` and `:237` perform consecutive two-second waits. `libs/draxul-host/src/nvim_host.cpp:182` calls shutdown synchronously, so closing an unresponsive Neovim pane can freeze the interface.

**Investigation**

- [x] Trace process, RPC reader, and pipe ownership during Windows pane closure.

**Fix strategy**

- [x] Transfer bounded process termination/reaping to a self-contained background owner.
- [x] Preserve cancellation, handle lifetime, and idempotent shutdown.

**Acceptance criteria**

- [x] Closing an unresponsive Neovim pane leaves other panes responsive.
- [x] The child is eventually reaped without leaked handles or worker access to destroyed state.
- [x] Preserve the existing nonblocking POSIX behavior.

## Windows closeout (2026-09-22)

The unresponsive-child regression proved prompt caller return and eventual
handle reaping in the final 48/48 products aggregate; same-cache smoke passed.
