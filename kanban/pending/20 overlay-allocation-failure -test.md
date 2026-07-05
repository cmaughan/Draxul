# Overlay allocation failure matrix

**Type:** test
**Priority:** 20
**Raised by:** GPT/Codex

## Gap

Existing tests cover some ToastHost/grid-handle failures, but command palette, chrome text, tooltip/diagnostics, and cross-overlay initialization/degradation are not covered as one explicit matrix.

## Implementation plan

- [ ] Inventory every overlay grid handle, texture/atlas, renderer attachment, and optional degradation path.
- [ ] Reuse existing fake renderer allocation controls rather than creating per-test mocks.
- [ ] Force failure for command palette, toast, chrome text, diagnostics, and tooltip resources one at a time.
- [ ] Assert initialization either rolls back atomically or leaves the documented reduced feature set with a single useful error.
- [ ] Verify input capture and frame requests are not left active for an overlay that failed to initialize.
- [ ] Add recovery coverage if a failed resource is meant to retry.

## Verification

- [ ] Run construct/shutdown repeatedly under ASan.
- [ ] Run with diagnostics/toasts enabled and disabled.
- [ ] Ensure existing ToastHost lifecycle tests remain the source for toast timing behavior.

## Acceptance criteria

- [ ] Every overlay has an explicit, tested allocation-failure contract.
- [ ] No null handle reaches a draw call.
- [ ] Failure leaves App shutdown safe and bounded.

## Dependencies and parallelism

Safety net before item 23; independent of the Unicode content fix in item 08.

<model>GPT-5 Codex</model>
