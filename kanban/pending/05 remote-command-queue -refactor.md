# Share remote terminal command queue policy

**Summary:** Make both remote-terminal connections use the same rules for handling queued input so their behaviour stays consistent as the code changes.

**Priority:** P1 — duplicated bounded-input rules can diverge across supported paths.  
**Source:** `libs/draxul-client/src/remote_session_coordinator.cpp`  
**Proposed by:** Codex 2; Claude 6, 31 narrowed. **Owner:** one client/terminal-host agent.  
**Evidence:** coordinator and `remote_terminal_host.cpp` repeat 128-command/48 KiB bounds, request IDs, resize replacement and scroll coalescing.

**Boundary verification**
- [ ] Pin framed input, retry-ID retention, mergeability, bounds and standalone fake-terminal behavior.
**Implementation and migration**
- [ ] Add a value-only queue in `draxul-client` with injected ID source; migrate host, then coordinator. Keep their locks, wakes, workers and transports.
**Unit tests**
- [ ] Test atomic multi-chunk input, saturation, coalescing, retry and IDs via client test-internals.
**Cross-platform validation**
- [ ] Retain coordinator and fake-terminal integration on Windows/macOS; run aggregate and same-cache smoke.
**Agent documentation and tooling**
- [ ] Document why the coordinator-free diagnostic path remains supported.
**Acceptance criteria**
- [ ] One queue policy serves both paths without changing their lifecycle or transport behavior.
