# Permit replacement at the pane limit

**Summary:** Allow an agent to replace an existing terminal even when the tab has reached its pane limit.

**Priority:** P2  
**Source:** `libs/draxul-server/src/topology_service.cpp`

**Evidence and trigger:** B34; the pane-count guard precedes replacement handling, rejecting an operation that adds no pane.

- [x] **Investigate:** Trace replacement and additional-pane allocation, including failure rollback.
- [x] **Fix:** Apply the count limit only to operations that increase pane count.
- [x] **Acceptance:** Replacement succeeds in a full valid tab, new-pane launch remains rejected, and invalid replacement targets remain rejected.
- [x] **Validation:** Run core aggregate tests and same-cache smoke.

Implementation notes (2026-10-10):
- The capacity and split-depth guards now apply only to sibling creation. Replacement still validates the target domain and identity before allocation.
- Allocation precedes topology mutation and old-terminal destruction, so a failed spawn leaves the original terminal and revision intact.
- Added a protocol-valid balanced 256-pane fixture covering replacement with stable pane/tree identities, rejected growth, missing/client-local targets, and failed allocation rollback. Aggregate validation remains pending.

## Final Debug validation (2026-10-10)

- Core + SatView + ScoreView aggregate: all 62 CTest entries passed in 45.57s.
- Same-cache native Metal startup smoke passed; the Debug app was explicitly
  linked in that cache first (3.63s).
- Basic native Metal snapshot comparison passed (0.0548% changed pixels).
- Controlled client concurrency checks passed 5 epoch-only repeats plus 15
  paired epoch/pre-wait repeats; prior aggregate failures and their proven
  synchronization corrections are retained in the session validation notes.
- Windows uses the same platform-neutral policy/geometry paths and normal CI.
  No separate Windows manual gate is introduced for these fixes.
- Final Release startup confirmation passed (see completed validation below).

## Completed validation

Final aggregate: 62/62 passed (45.57s). Two earlier 62-entry passes took
53.08s and 52.01s and exposed the documented client test synchronization races;
the second also observed a 262ms/250ms checkpoint timing failure that passed
both the first and final aggregate. Focused concurrency diagnosis: 5 epoch
repeats plus 15 paired epoch/pre-wait repeats, all passed after corrections
(20.44s total tests). These repeats overlap aggregate coverage intentionally
while diagnosing failures.

Debug startup smoke passed (~1.2s); basic native Metal comparison passed
(~1.8s) and registered `draxul-render-basic-view` CTest passed (1.66s).
The comparison was repeated once to confirm the registered CTest gate.
Final Release build/configure passed (63.95s total: configure/generate 10.3s,
compile/link ~53.6s); Release isolated startup smoke passed (~0.7s). The first
custom Release runtime exceeded the Unix socket path limit; the standard short
isolated runner succeeded, and no failed helper remains. Debug's initial
aggregate compile took 271.07s after the existing Release cache was reconfigured;
warm aggregate builds and the explicit app link reused that cache. One initial
sandbox build was blocked by compiler-cache access before tests; the authorized
run used the existing compiler cache. Windows/remote CI was not run locally.

Detailed session logs: `/tmp/draxul-easy-cards-aggregate-pass.log`,
`/tmp/draxul-easy-cards-render-ctest.log`, and
`/tmp/draxul-easy-cards-release-smoke.log`.
