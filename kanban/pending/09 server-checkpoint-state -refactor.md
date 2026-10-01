# Own server checkpoint transitions in one collaborator

**Summary:** Give one component responsibility for scheduling and tracking session saves so save failures and shutdown behaviour are easier to understand and test.

**Priority:** P1 — write state and shutdown flush span multiple kernel files.  
**Source:** `libs/draxul-server/src/server_kernel_sessions.cpp`  
**Proposed by:** Claude 9. **Owner:** one server agent. **Related:** card 10.  
**Evidence:** checkpoint state is in `server_kernel_impl.h`, writer in sessions, and scheduling/final flush in lifecycle.

**Boundary verification**
- [ ] Characterize revision, in-flight, failed save, last-good and shutdown-deadline behavior.
**Implementation and migration**
- [ ] Add private `SessionCheckpointer` with injected save, clock and executor; migrate scheduling and result collection without changing durable save behavior.
**Unit tests**
- [ ] Use fake time/executor for N-to-N+1 flush, failure and deadline cases; retain IPC persistence tests.
**Cross-platform validation**
- [ ] Preserve Windows replacement and POSIX durability; run aggregate and same-cache smoke.
**Agent documentation and tooling**
- [ ] Document state ownership and shutdown ordering.
**Acceptance criteria**
- [ ] Status strings and last-good checkpoint semantics remain stable.
