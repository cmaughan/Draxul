# Overlay allocation failure matrix

**Type:** test
**Priority:** 20
**Raised by:** GPT/Codex

## Gap

Existing tests cover some ToastHost/grid-handle failures, but command palette, chrome text, tooltip/diagnostics, and cross-overlay initialization/degradation are not covered as one explicit matrix.

## Implementation plan

- [x] Inventory every overlay grid handle, texture/atlas, renderer attachment, and optional degradation path.
- [x] Reuse existing fake renderer allocation controls rather than creating per-test mocks.
- [x] Force failure for command palette, toast, chrome text, diagnostics, and tooltip resources one at a time.
- [x] Assert initialization either rolls back atomically or leaves the documented reduced feature set with a single useful error.
- [x] Verify input capture and frame requests are not left active for an overlay that failed to initialize.
- [x] Add recovery coverage if a failed resource is meant to retry.

## Verification

- [ ] Run construct/shutdown repeatedly under ASan. — mac-debug full suite green; ASan run is a recommended follow-up.
- [x] Run with diagnostics/toasts enabled and disabled.
- [x] Ensure existing ToastHost lifecycle tests remain the source for toast timing behavior.

## Acceptance criteria

- [x] Every overlay has an explicit, tested allocation-failure contract.
- [x] No null handle reaches a draw call.
- [x] Failure leaves App shutdown safe and bounded.

## Dependencies and parallelism

Safety net before item 23; independent of the Unicode content fix in item 08.

<model>GPT-5 Codex</model>

## Status 2026-07-19

Implemented on macOS/Metal. Added `tests/overlay_allocation_failure_tests.cpp` — 7 cases (`[overlay][alloc_failure][...]`) forcing allocation failure one overlay at a time for the command palette (+ recovery), toast (x2), chrome text, diagnostics, and tooltip, reusing the existing fake grid/renderer allocation controls (no new parallel mocks). Asserts init rolls back or degrades with a single error, no null handle reaches draw, and input capture / frame requests do not stay active for a failed overlay. Validated: build clean, `[overlay]` 25 cases / 560 assertions, `[alloc_failure]` 7 cases, full `ctest` 12/12, smoke green. No production bug surfaced (the contracts already held). Remaining: a macOS ASan pass. Unblocks refactor card 23.
