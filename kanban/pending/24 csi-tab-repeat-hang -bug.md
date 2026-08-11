# Bound CSI tab-repeat processing

**Severity:** HIGH  
**Type:** Bug

## Bug description

CHT and CBT process one loop iteration per untrusted CSI count even after reaching the grid edge, allowing a short output sequence to stall terminal processing for billions of iterations.

**Trigger:** Emit `ESC[2000000000I` or the equivalent large CBT sequence.

## Investigation

- [ ] Add focused terminal-core tests for zero, normal, boundary, and `INT_MAX` repeat counts.
- [ ] Verify expected tab-stop semantics at columns zero and the final grid column.
- [ ] Measure that hostile inputs complete within a strict time bound.

## Fix strategy

- [ ] Compute the destination tab stop in constant time where practical.
- [ ] Otherwise cap iterations to the number of reachable tab stops.
- [ ] Reuse parameter normalization from `kanban/pending/22 csi-cursor-parameter-overflow -bug.md`.

## Acceptance criteria

- [ ] Runtime is bounded by grid dimensions rather than the supplied repeat count.
- [ ] Large CHT and CBT sequences leave the cursor at the correct boundary.
- [ ] Local and server terminal-core tests complete without stalls.
