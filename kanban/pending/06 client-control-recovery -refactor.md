# Inject client control requests and recovery policy

**Priority:** P1 — direct static requests force socket tests and duplicate handshake retry.  
**Source:** `libs/draxul-client/src/server_control_channel.cpp`  
**Proposed by:** Claude 11. **Owner:** one client agent. **Related:** card 05.  
**Evidence:** `ServerControlChannel` calls static `ControlClient::request`; remote terminal client repeats token/handshake refresh logic.

**Boundary verification**
- [ ] Record retry budgets, token refresh, endpoint naming and concurrent identity behavior.
**Implementation and migration**
- [ ] Inject request and refresh/probe operations into the control channel; compose compatible callers incrementally. Keep native endpoint naming in the production adapter.
**Unit tests**
- [ ] Script handshake failure, refresh, newer identity, retry and final error mapping without sockets; retain IPC coverage.
**Cross-platform validation**
- [ ] Test Windows namespaced endpoints and POSIX sockets, aggregate and same-cache smoke.
**Agent documentation and tooling**
- [ ] Expose only the test seam required by callers.
**Acceptance criteria**
- [ ] Client recovery has one policy and production timing/error behavior is preserved.
