# WI 123 — UiRequestWorker Overlapping Requests and Cancellation Test

**Type:** Test  
**Severity:** Medium (correctness of async request coalescing)  
**Source:** Gemini review  
**Authored by:** claude-sonnet-4-6

---

## Problem

`UiRequestWorker` handles async UI requests (e.g. treesitter scans, resize coalescing). The existing test `17 ui-request-worker-shutdown -test.md` covers shutdown but does not cover:
- What happens when a second request arrives while the first is still in flight
- Whether requests are ordered correctly (FIFO? deduplicated? last-wins?)
- Whether cancellation (via shutdown signal or pane close) is handled cleanly

From Gemini: "A `UiRequestWorker` test for overlapping requests, ordering, and cancellation semantics."

---

## What to Test

```
Ordering:
  - Submit request A then B while A is in flight
  - Verify B is not processed before A completes (or verify documented ordering policy)

Coalescing:
  - Submit request A, then same-type request A' before A starts
  - Verify only one execution occurs (if coalescing is intended)

Cancellation:
  - Submit request A (slow), cancel before it starts → A never executes
  - Submit request A (slow), cancel while in flight → A execution aborts cleanly

Shutdown:
  - Worker shutdown while queue is non-empty → all pending requests cancelled, no crash
  - Worker shutdown while request in flight → waits for completion or aborts safely
```

---

## Implementation Notes

- Use a fake "work" callback with a controllable delay (barrier / semaphore)
- Test file: extend `tests/ui_request_worker_tests.cpp` or create it
- No Neovim or renderer required — pure threading test
- Run under TSan (`mac-tsan` preset, WI 112) to catch races

---

## Acceptance Criteria

- [x] Ordering, coalescing, cancellation, and shutdown scenarios are covered
- [x] Tests run under `ctest`
- [x] CI green

TSan validation of these tests is rolled into the follow-up WI 134
(tsan-validation-and-ci-wiring), which runs the whole `draxul-tests`
suite under `mac-tsan` and triages anything it finds.

## Implementation

Added `tests/ui_request_worker_overlap_tests.cpp` with six `[ui][overlap]`
Catch2 test cases exercising the worker against a new `GatedFakeRpc` that
provides per-call gating so the test can deterministically hold a request
in-flight while submitting further requests:

- "does not start a second request before the in-flight one completes" —
  verifies that submitting B while A is in-flight keeps A as the only
  in-flight call; B only starts after A is released.
- "coalesces a burst submitted during an in-flight request" — 10 bursts
  submitted during an in-flight A collapse to exactly one follow-up call
  carrying the latest cols/rows (latest-wins slot semantics).
- "drops pending requests when stopped during an in-flight call" — stop()
  called while A is in-flight waits for A to complete but drops a B that
  was queued behind it.
- "shutdown drops pending request that never started" — stress-loop of
  submit+stop that verifies at most one RPC call ever runs (no crash, no
  replay after shutdown).
- "ignores post-stop resize requests" — confirms post-stop submissions
  are silently dropped.
- "start after stop accepts new requests cleanly" — restart does not
  replay prior pending state.

TSan validation is deferred until WI 112 lands the `mac-tsan` preset.

---

## Interdependencies

- Related to **WI 17** (`ui-request-worker-shutdown` test — extend or complement it).
- **WI 112** (TSan preset) makes it easier to validate threading; add to TSan CI once WI 112 is green.
