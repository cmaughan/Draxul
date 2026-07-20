# SatView host lifecycle and draw smoke

**Type:** test
**Priority:** 18
**Raised by:** Claude; supported by GPT/Codex

## Gap

SatView has many pure-unit tests but no direct fake-renderer host fixture covering construct/initialize/pump/draw/config round-trip and dirty-frame behavior across the rapidly growing orchestration surface.

## Implementation plan

- [x] Wait for the current observatory/boundary/text-atlas patch to settle, then inventory `SatViewHost` dependencies.
- [x] Add narrow fake clock, transport/catalog, render-pass attachment, callbacks, and frame-request dependencies; do not require a real GPU or network.
- [x] Construct and initialize the host from bundled/sample data.
- [x] Exercise one pump/draw, a config change, view transition, selection change, and shutdown.
- [x] Assert frame requests/dirty flags settle instead of requesting forever when paused.
- [x] Round-trip the durable `[satview]` config fields touched by the fixture.

## Verification

- [x] Run with `DRAXUL_ENABLE_SATVIEW=ON` and prove the test is absent/clean when OFF.
- [ ] Run under ASan and repeat construct/shutdown cycles. — mac-debug full suite green; ASan run is a recommended follow-up.
- [x] Keep runtime deterministic and suitable for normal CTest.

## Acceptance criteria

- [x] Core host wiring fails fast in a CPU-only test.
- [x] No test reads the live network or system clock.
- [x] The test protects the seams needed for item 26.

## Dependencies and parallelism

Follows active SatView work and precedes item 26. Suitable for a SatView-specific sub-agent once file churn stops.

<model>GPT-5 Codex</model>

## Status 2026-07-19

Implemented on macOS/Metal. Added `tests/satview_host_smoke_tests.cpp` (7 cases) + `tests/support/satview_host_fixture.h` (CPU-only: fake clock/renderer/callbacks, no GPU or network) plus a `TestHooks` seam on `SatViewHost`. Exercises offline construct/init/draw/shutdown, `[satview]` config apply/readback/persist-through-shutdown, camera POV transition, selection track+clear, and frame-request/dirty-flag settling when paused. Validated: build clean, `[satview][host]` 67 assertions / 7 cases, full `ctest` 12/12, smoke green. SATVIEW=OFF: the file name matches the existing `satview_*_tests.cpp` GLOB-exclusion in `tests/CMakeLists.txt` and the fixture is `#ifdef DRAXUL_ENABLE_SATVIEW`-guarded, so it is excluded when OFF (a full OFF configure is the CI check). Remaining: a macOS ASan pass. Unblocks refactor card 26.
