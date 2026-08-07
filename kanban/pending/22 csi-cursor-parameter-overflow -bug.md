# Saturate large CSI cursor parameters

**Severity:** CRITICAL  
**Type:** Bug

## Bug description

Several CSI cursor and erase operations add an untrusted positive `int` to the current row or column before clamping, permitting signed-overflow undefined behavior.

**Trigger:** Emit a maximum-width parameter such as `ESC[2147483647C` while the cursor column is nonzero.

## Investigation

- [ ] Audit every CSI arithmetic expression involving parsed parameters.
- [ ] Add extreme-value tests for CUD, CUF, CNL, origin-mode CUP/HVP, and ECH.
- [ ] Confirm out-of-range `std::from_chars` behavior is handled consistently.

## Fix strategy

- [ ] Introduce checked or saturating helpers for relative and absolute CSI coordinates.
- [ ] Clamp before arithmetic or use a wider unsigned intermediate with validated bounds.
- [ ] Keep the implementation coordinated with `kanban/pending/24 csi-tab-repeat-hang -bug.md`.

## Acceptance criteria

- [ ] `INT_MAX` parameters never overflow and always produce a valid grid coordinate or bounded range.
- [ ] UBSan coverage reports no signed-overflow failures for hostile CSI inputs.
- [ ] Existing terminal-core behavior and CSI tests remain passing.
