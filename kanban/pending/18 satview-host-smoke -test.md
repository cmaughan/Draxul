# SatView host lifecycle and draw smoke

**Type:** test
**Priority:** 18
**Raised by:** Claude; supported by GPT/Codex

## Gap

SatView has many pure-unit tests but no direct fake-renderer host fixture covering construct/initialize/pump/draw/config round-trip and dirty-frame behavior across the rapidly growing orchestration surface.

## Implementation plan

- [ ] Wait for the current observatory/boundary/text-atlas patch to settle, then inventory `SatViewHost` dependencies.
- [ ] Add narrow fake clock, transport/catalog, render-pass attachment, callbacks, and frame-request dependencies; do not require a real GPU or network.
- [ ] Construct and initialize the host from bundled/sample data.
- [ ] Exercise one pump/draw, a config change, view transition, selection change, and shutdown.
- [ ] Assert frame requests/dirty flags settle instead of requesting forever when paused.
- [ ] Round-trip the durable `[satview]` config fields touched by the fixture.

## Verification

- [ ] Run with `DRAXUL_ENABLE_SATVIEW=ON` and prove the test is absent/clean when OFF.
- [ ] Run under ASan and repeat construct/shutdown cycles.
- [ ] Keep runtime deterministic and suitable for normal CTest.

## Acceptance criteria

- [ ] Core host wiring fails fast in a CPU-only test.
- [ ] No test reads the live network or system clock.
- [ ] The test protects the seams needed for item 26.

## Dependencies and parallelism

Follows active SatView work and precedes item 26. Suitable for a SatView-specific sub-agent once file churn stops.

<model>GPT-5 Codex</model>
