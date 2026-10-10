# Honor pending wakes in the session poll worker
**Summary:** Remember background wake requests so input and shutdown are not delayed during recovery.

**Priority:** P2  
**Source:** `libs/draxul-client/src/remote_session_coordinator.cpp`  
**Reported by:** Claude M3; consensus F46.

**Evidence and trigger:** Lines 2504 and 2559 wait without a predicate despite remembered pending state. A wake arriving before either wait can be lost, including during five-second recovery backoff.

- [x] **Investigate:** Trace poll success/failure waits, pending state, input wake, stopping, and joining.
- [x] **Fix:** Use the existing predicate-based worker wait helper and consume pending state consistently.
- [x] **Acceptance:** A wake published before either wait is honored; stopping interrupts recovery backoff promptly.
- [x] **Acceptance:** Healthy idle polling retains intended pacing without spinning.
- [x] **Validation:** Run core aggregate tests and same-cache smoke with controlled pre-wait input/stop interleavings.

Implementation notes (2026-10-10):
- Both recovery and post-response waits use `wait_for_worker`, whose mutex-protected predicate honors pending wake or stopping and consumes pending state once.
- Added controlled integration interleavings that pause the worker in recovery publication immediately before its wait, then publish queued terminal input or stop before releasing it. Recovery is seeded to its multi-second ceiling so a lost notification is observable.
- Added healthy idle polling coverage after remembered registration/unregistration wakes to ensure pending state does not cause a spin. Aggregate validation remains pending.
- The first aggregate exposed a pre-existing epoch-replacement test race: its wait accepted old mailbox snapshots as soon as shared recovery learned the successor epoch. The test now waits for successor-tagged topology/agent publications and the successor terminal title before asserting convergence.
- Focused epoch-replacement diagnosis passed 5/5 repeats (seeds 3930280595–3930280599, 25 assertions per run, 4.03 seconds) after the synchronization fix. The sandbox-only attempt could not start local IPC; the permitted unsandboxed run succeeded. Shared aggregate/smoke gate remains owned by the parent task.
- The second aggregate found a test setup race in the new recovery gate: an in-flight successful response could reset recovery after the UI seeded its backoff. Seeding now happens inside the failing request handler, after all preceding responses were accepted, so the gated failure deterministically reaches the backoff ceiling.
- Corrected pre-wait recovery and epoch-replacement tests passed 15/15 focused repeats together (2 cases and 41 assertions per run, seeds 1551514904–1551514918, 16.41 seconds). Fresh aggregate validation remains pending.

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
