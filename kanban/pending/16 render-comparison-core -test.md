# Test render comparison and finalization

**Type:** test
**Priority:** 16
**Raised by:** Claude

## Gap

The image-diff and result-finalization code in `draxul-render-test` gates every snapshot but lacks direct boundary tests. A bug there can make all scenario results falsely pass or fail.

## Implementation plan

- [ ] Identify the pure comparison/finalization entry points in `libs/draxul-render-test/src/render_test.cpp`.
- [ ] Add table-driven tests for equal frames, dimension mismatch, channel order, tolerance just below/at/above the boundary, changed-pixel thresholds, zero threshold, and fully different images.
- [ ] Cover report fields, diff image generation, output paths, and error propagation independently of a GPU/window.
- [ ] Make rounding and comparison semantics explicit in names/comments.
- [ ] Add overflow guards for pixel-count/byte-count arithmetic if tests expose unchecked multiplication.

## Verification

- [ ] Run the new pure tests without Neovim or a renderer.
- [ ] Run one passing and one intentionally failing temporary scenario and compare the emitted report to the pure result.
- [ ] Run the normal render scenarios without blessing references.

## Acceptance criteria

- [ ] Every threshold/dimension branch has a deterministic unit test.
- [ ] Diff/report outputs agree with the pass/fail decision.
- [ ] Existing snapshot results do not change without an explained bug fix.

## Dependencies and parallelism

Independent and a good small sub-agent task. Complements item 03.

<model>GPT-5 Codex</model>
