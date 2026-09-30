# Hide native PTY types behind a process factory

**Priority:** P1 — server runtime publicly selects ConPTY/Unix implementations.  
**Source:** `libs/draxul-terminal-process/include/draxul/conpty_process.h`  
**Proposed by:** Claude 10, narrowed. **Owner:** one terminal-process/server-runtime agent.  
**Evidence:** public ConPTY header exposes `windows.h` and `HANDLE`; Unix spawn differs; server runtime switches native process and spawn logic.

**Boundary verification**
- [ ] Pin shell candidate order, environment, login behavior, callback and writer-retirement order.
**Implementation and migration**
- [ ] Add neutral spawn options/process factory and pure shell resolver; inject factory into `ServerTerminalRuntime`. Keep concrete native types private and runtime state/API intact.
**Unit tests**
- [ ] Fake process and cross-platform shell-resolution cases; retain ConPTY, PTY and fd-inheritance integration.
**Cross-platform validation**
- [ ] Verify Windows cancel-before-handle-destruction and POSIX close-before-join, aggregate and smoke.
**Agent documentation and tooling**
- [ ] Document borrowed writer-handle and shutdown ownership.
**Acceptance criteria**
- [ ] Public process API contains no native handle types; no terminal behavior changes.
