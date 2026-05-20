# 06 Test Seam Cleanup And Lifecycle Coverage

## Why This Exists

The test suite is already strong for a GUI app, but the next step is better ownership and more failure/lifecycle coverage.

Current verified issues:

- tests still pull in private `src` directories
- tests compile `app/render_test.cpp` directly
- missing lifecycle/failure cases include shutdown latency, font resize chains, startup resize deferral, and config/parser round trips

## Goal

Move tests closer to supported seams and fill the remaining lifecycle/failure holes.

## Implementation Plan

1. ~~Reduce private-source reach-through.~~ **ASSESSED — minimal change made**
   - `libs/draxul-font/src`, `libs/draxul-renderer/src`, `libs/draxul-nvim/src` are needed by tests that directly test internal APIs (font rasterization, renderer state, RPC codec). Moving them would require exposing internal headers publicly or significant PIMPL refactoring — deferred.
   - `../app/render_test.cpp` compiled into tests is the only practical way to test scenario parsing without making it a separate library — acceptable as-is.
   - Added `../app/app_config.cpp` and `../app/cursor_blinker.cpp` to the test build as they now have direct test coverage.
2. ~~Add lifecycle-focused coverage.~~ **PARTIALLY DONE**
   - Added: `nvim rpc close() unblocks an in-flight request without waiting for timeout` — verifies #00 shutdown fix
   - Startup resize deferral and font-size change chain are App-level; require a full initialized App (window, renderer, nvim) — deferred to integration test work
3. ~~Add parser/config round-trip coverage.~~ **DONE**
   - Added `AppConfig::parse(std::string_view)` and `AppConfig::serialize()` to expose testable seam
   - `load()`/`save()` now delegate to these (no behavior change)
   - 7 new tests in `tests/app_config_tests.cpp`: defaults, all-fields, comments/blanks, clamping, round-trip, serialize-clamps

## Sub-Agent Split

- one agent can add tests while another extracts seams, but only after the seam direction is agreed
