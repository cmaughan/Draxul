# Enforce private include and test boundaries

**Priority:** P2 — relative private-source includes and broad exported usage requirements hide coupling.  
**Source:** `cmake/CheckDependencyBoundaries.cmake`  
**Proposed by:** Claude 30, 32 narrowed. **Owner:** one core CMake/ownership agent. **Follows:** cards 02–04 where affected.  
**Evidence:** host leaves share include roots; tests reach client, NanoVG, Kanban and server `src/` by relative paths; app exports its source directory and many links.

**Boundary verification**
- [ ] Inventory each private include and public-header dependency before changing link visibility.
**Implementation and migration**
- [ ] Add narrow `*-test-internals` targets and missing link-isolation consumers; migrate private includes; demote only usage requirements proven private.
**Unit tests**
- [ ] Add configure checks against new relative private-source includes and link/compile isolation.
**Cross-platform validation**
- [ ] Configure Windows/macOS and run core aggregate and smoke.
**Agent documentation and tooling**
- [ ] Document public-header versus test-internals rule.
**Acceptance criteria**
- [ ] Tests have explicit private access; production consumers compile from declared interfaces.
