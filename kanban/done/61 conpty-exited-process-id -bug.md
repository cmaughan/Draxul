# Stop publishing exited ConPTY process identifiers

**Severity:** MEDIUM  
**Reported by:** Claude Fable 5.1

`libs/draxul-terminal-process/src/conpty_process.cpp:973` returns retained `dwProcessId` after the child exits or handles are closed. The server publishes that recyclable PID, unlike the POSIX implementation’s zero result after confirmed exit.

**Investigation**

- [x] Follow PID publication through natural exit, abnormal exit, shutdown, and restart.

**Fix strategy**

- [x] Return zero after confirmed exit and clear retired process identity.
- [x] Keep unknown process-status handling distinct from confirmed exit.

**Acceptance criteria**

- [x] Exited panes never advertise a stale live PID.
- [x] Restart publishes the new PID.
- [x] Coordinate status semantics with
      `kanban/pending/29 conpty-process-status-query-failure -bug.md`: a failed
      status query remains conservatively running and retains the published PID,
      while a confirmed exit records its code and publishes PID zero.
