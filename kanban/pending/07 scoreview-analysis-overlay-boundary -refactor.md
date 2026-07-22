# Split ScoreView analysis-overlay build from NanoVG drawing

**Type:** refactor
**Priority:** P2 / sequence 07
**Raised by:** Claude
**Consensus:** `plans/reviews/review-refactor-consensus.md`, Accepted 8

## Goal

Compile pure analysis-overlay geometry construction into `draxul-scoreview` and
keep NanoVG replay private to `draxul-scoreview-host`, allowing overlay geometry
tests to use the lighter core test target.

## Boundary verification

- [ ] Verify `analysis_overlay.cpp` construction and draw regions have no hidden shared mutable state.
- [ ] Inventory all declarations/callers in `analysis_overlay.h`, `ScoreHost`, and `ScorePresentation`.
- [ ] Confirm core construction requires only draw-list, timemap, highlight, and learning profile values.
- [ ] Confirm drawing alone needs `NVGcontext` and `ScoreTextFonts`.
- [ ] Record current overlay test cases and host-level replay coverage before moving them.

## Implementation and migration

- [ ] Keep `AnalysisOverlay` values and `build_analysis_overlay` in the public core header.
- [ ] Move construction to `analysis_overlay_build.cpp` in `draxul-scoreview`.
- [ ] Move NanoVG replay to `analysis_overlay_draw.cpp` and a host-private draw header.
- [ ] Update `ScoreHost`/`ScorePresentation` includes and target source lists.
- [ ] Move `scoreview_overlay_tests.cpp` from host test sources to core ScoreView test sources.
- [ ] Do not reopen or redesign completed ScoreView controllers from
  `kanban/done/21 scoreview-host-decomposition -refactor.md`.

## Unit tests

- [ ] Run every existing overlay geometry assertion through `draxul-test-scoreview`.
- [ ] Add/retain one host-level draw smoke case for NanoVG replay if current coverage exercises drawing.
- [ ] Verify no core test links SDL, ImGui, NanoVG, host, or microphone code for overlay construction.
- [ ] Build both `draxul-test-scoreview` and `draxul-test-scoreview-host`.
- [ ] Run CTest labels `scoreview` and `scoreview-host`.

## Cross-platform validation

- [ ] Build and run focused tests on Windows and macOS.
- [ ] Confirm NanoVG overlay output/ordering remains identical on both platforms.
- [ ] Confirm no Vulkan/Metal API or resource type enters the pure build path.
- [ ] Do not touch microphone/audio code; verify macOS TCC ordering remains unaffected.

## Agent documentation and tooling

- [ ] Update Score nested guidance/module map if it lists overlay ownership.
- [ ] Ensure label tooling builds core-only overlay tests for `--label scoreview`.

## Acceptance criteria

- [ ] Pure overlay tests link to `draxul-scoreview`, not `draxul-scoreview-host`.
- [ ] NanoVG replay remains host-private and behaviorally unchanged.
- [ ] Public analysis value/build API remains source compatible.
- [ ] Focused ScoreView core/host tests, full tests, and smoke pass.

## Dependencies and ownership

Depends on `kanban/pending/00 internal-target-build-policy -refactor.md`. One
ScoreView owner performs the declaration/source split. Core test migration and a
host draw smoke can proceed independently after declarations settle.
