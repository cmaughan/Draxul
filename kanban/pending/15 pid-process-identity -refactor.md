# Centralize PID start identity

**Summary:** Use one method to identify a running process by its start time so client and server checks agree even when the operating system reuses a process number.

**Priority:** P2 — client and server duplicate OS token formatting.  
**Source:** `libs/draxul-types/src/process_util.cpp`  
**Proposed by:** Codex 10. **Owner:** one process/core agent.  
**Evidence:** `server_client.cpp` and `server_kernel_lifecycle.cpp` separately derive macOS/Linux start tokens; process utility already has a Windows handle form.

**Boundary verification**
- [ ] Compare exact token bytes and PID-reuse protections.
**Implementation and migration**
- [ ] Add PID lookup to existing process utility and migrate producer/consumer; retain client liveness and held-handle checks.
**Unit tests**
- [ ] Current/missing PID and token-format cases plus lifecycle/discovery integration.
**Cross-platform validation**
- [ ] Verify Windows/macOS behavior, aggregate and smoke.
**Agent documentation and tooling**
- [ ] Document token ownership in process utility.
**Acceptance criteria**
- [ ] One implementation formats process identity without changing tokens.
