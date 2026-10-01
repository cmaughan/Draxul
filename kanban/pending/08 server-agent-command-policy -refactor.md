# Isolate server agent command policy

**Summary:** Separate the rules for checking and retrying agent commands from the server's main dispatcher so those rules can be tested without launching real processes.

**Priority:** P1 — validation and replay policy is entangled with kernel routing.  
**Source:** `libs/draxul-server/src/server_kernel_requests.cpp`  
**Proposed by:** Codex 9; Claude 8 narrowed. **Owner:** one server agent.  
**Evidence:** agent route handles request-ID validation, replay and launch/restart/input effects in the central dispatcher.

**Boundary verification**
- [ ] Inventory authentication order, error precedence, generation checks and successful-only replay keys.
**Implementation and migration**
- [ ] Add a private agent-command collaborator with typed effect/snapshot operations; migrate one operation family at a time. Keep Session lookup, state thread and terminal ownership in kernel.
**Unit tests**
- [ ] Fake operations for duplicate success, failed noncache, invalid IDs, generation and launch/restart mapping; retain process integration.
**Cross-platform validation**
- [ ] Run server aggregate and same-cache smoke on both platforms.
**Agent documentation and tooling**
- [ ] State the private policy/kernel split in server guidance.
**Acceptance criteria**
- [ ] Agent policy is isolated without a universal router rewrite or changed replay semantics.
