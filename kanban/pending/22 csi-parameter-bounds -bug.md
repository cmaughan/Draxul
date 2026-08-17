# Bound hostile CSI parameters

**Severity:** CRITICAL
**Type:** bug

## Bug

Relative cursor operations add an untrusted positive `int` before clamping, which can
overflow for inputs such as `ESC[2147483647C`. CHT/CBT also execute one iteration per
supplied count after the cursor reaches the grid edge, so a tiny input can stall terminal
processing for billions of iterations. Both defects share the same parameter-normalization
boundary in `libs/draxul-terminal-core/src/terminal_core_csi.cpp`.

## Work

- [ ] Audit every CSI arithmetic expression involving parsed parameters, including
      CUD, CUF, CNL, CUP/HVP, ECH, CHT, and CBT.
- [ ] Introduce checked/saturating coordinate helpers and constant-time or grid-bounded
      tab-stop movement.
- [ ] Cover zero, ordinary, boundary, out-of-range parse, and `INT_MAX` parameters.
- [ ] Verify hostile inputs complete within a strict time bound and UBSan reports no
      signed overflow.

## Acceptance criteria

- [ ] Runtime is bounded by grid dimensions, not the supplied repeat count.
- [ ] Extreme parameters always yield a valid coordinate/range with correct tab semantics.
- [ ] Local and server terminal-core tests remain green.
