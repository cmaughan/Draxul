# Test render comparison and finalization

**Type:** test
**Priority:** 16
**Raised by:** Claude

## Gap

The image-diff and result-finalization code in `draxul-render-test` gates every snapshot but lacks direct boundary tests. A bug there can make all scenario results falsely pass or fail.

## Implementation plan

- [x] Identify the pure comparison/finalization entry points in `libs/draxul-render-test/src/render_test.cpp`.
- [x] Add table-driven tests for equal frames, dimension mismatch, channel order, tolerance just below/at/above the boundary, changed-pixel thresholds, zero threshold, and fully different images.
- [x] Cover report fields, diff image generation, output paths, and error propagation independently of a GPU/window.
- [x] Make rounding and comparison semantics explicit in names/comments.
- [x] Add overflow guards for pixel-count/byte-count arithmetic if tests expose unchecked multiplication.

## Verification

- [x] Run the new pure tests without Neovim or a renderer.
- [x] Run one passing and one intentionally failing temporary scenario and compare the emitted report to the pure result.
- [x] Run the normal render scenarios without blessing references.

## Acceptance criteria

- [x] Every threshold/dimension branch has a deterministic unit test.
- [x] Diff/report outputs agree with the pass/fail decision.
- [x] Existing snapshot results do not change without an explained bug fix.

## Dependencies and parallelism

Independent and a good small sub-agent task. Complements item 03.

<model>GPT-5 Codex</model>

## Status 2026-07-19

Implemented on macOS/Metal. Exposed the pure `compare_frames` + `RenderDiff` via `render_test.h` (moved `compare_frames` out of the file-local anonymous namespace; no behavior change) and added `tests/render_comparison_tests.cpp` — 8 table-driven cases / 47 assertions covering identical frames, dimension mismatch, invalid frame, channel-order (red/blue swap), tolerance strict-`>` boundary (delta == vs > tolerance), inclusive `<=` changed-pixel threshold (below/at/above + zero), fully-different (100%), diff-image dimensions/contents, and mean/max channel diff. No GPU/window/Neovim. Validated: build clean, `[render_compare]` 47 assertions, full `ctest` 12/12 (the 5 render scenarios still pass unblessed), smoke green.
