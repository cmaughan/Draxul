# Modularize the test targets

**Type:** refactor
**Priority:** 35
**Raised by:** GPT/Codex

## Goal

The single `draxul-tests` executable links App and every enabled product module, increasing link time and obscuring optional-module coverage. Split by stable target boundary while preserving shared fixtures and simple developer commands.

## Implementation plan

- [x] Measure current configure/build/link/test times and classify test sources by owning library/module.
- [x] Create shared test-support target(s) for existing fakes/helpers without duplicating implementations.
- [x] Introduce focused executables/CTest labels for core, app/session, Markdown/Kanban, MegaCity, SatView, and ScoreView in stages.
- [x] Link a ScoreView host-focused target to `draxul-scoreview-host` so microphone/NanoVG/host lifecycle tests do not force every core test to take the full product dependency.
- [x] Link each test target only to the public/internal test interface it needs; remove private `src/` includes by reopening the existing boundary card where required.
- [x] Keep a top-level `draxul-tests` build target that depends on all enabled test executables for workflow compatibility.
- [x] Update sanitizer, coverage, RPC-helper dependencies, scripts, and CI discovery.
- [x] Ensure optional-off configurations omit only their own test targets, including `DRAXUL_ENABLE_SCOREVIEW=OFF`.

## Tests and acceptance

- [x] Compare test case/tag counts before and after; no case disappears.
- [x] `ctest -j` can run independent targets concurrently and reports module labels.
- [x] Windows/macOS presets, sanitizers, coverage, `t.bat`/`t.sh`, and smoke remain usable.
- [x] Record measured link/test improvement or revert splits that add complexity without benefit.

## Implementation record (2026-07-21)

- Baseline monolith: 190 C++ source files, 1,637 listed cases, 167 tags, and a
  49,091,072-byte Debug executable.
- Current layout: seven executables (`core`, `app/session`, `markdown/kanban`,
  `megacity`, `satview`, `scoreview`, `scoreview-host`) behind the compatible
  `draxul-tests` aggregate, plus 22 discovered CTest entries and explicit
  module labels.
- All seven executables build and link independently on Windows. The final
  aggregate build also incorporated the concurrently added Chrome/session
  tests. A one-file core edit compiled and relinked only `draxul-test-core` in
  1.93 seconds; that executable is 27,583,488 bytes (44% smaller than the old
  monolith).
- The first parallel modular CTest pass completed in 28.28 seconds and proved
  concurrent labeled execution. It did not qualify as the final acceptance
  run: concurrent config/Chrome changes had failing snapshots, and it exposed
  an existing invalid iterator in ScoreView analysis. Test execution was then
  stopped at the user's request; the iterator was fixed separately and the
  affected targets were rebuilt without launching executables.
- Sanitizer/coverage helpers are applied to every executable and the RPC
  helper; macOS LCOV export now combines all enabled test binaries. Existing
  `t.bat`, `t.sh`, and unit scripts continue to build the aggregate target.
- Optional products are gated around both target creation and CTest discovery.
  A fresh all-optionals-off configure was attempted but dependency setup timed
  out after 300 seconds, so that configuration still needs a successful live
  configure before this card moves to `done`.
- Renderer, runtime-support, MegaCity, SatView, and ScoreView-host white-box
  access now goes through explicit `*-test-internals` interface targets. Test
  targets no longer name any library/module `src/` directory, and the two tests
  that used repository-relative private-header includes now use those named
  contracts.
- Configure now proves that the focused lists partition every `*_tests.cpp`
  source exactly once, then scans the sources without launching a binary. The
  modular-split baseline is 194 sources, 1,861 static `TEST_CASE`
  registrations, and 237 unique tag-shaped string tokens across all platforms
  and optional products; a future drop fails configuration. This complements
  the pre-split Windows runtime inventory of 1,637 active cases and 167 active
  tags, whose lower totals reflect compile-time platform/feature selection.
- Per the user's request after a Debug assertion surfaced, final validation is
  build/configure-only in this session: no test helper, unit-test executable,
  smoke process, or application is launched.
- `cmake -S . -B build` passed on Windows with every optional product enabled
  and printed the expected static inventory. The Release `draxul-test-app`
  target compiled the new session transport test and linked successfully with
  project references disabled. A narrow Release-only core build was stopped
  after the 124-second command timeout while compiling; its exact CMake/MSBuild
  process tree was terminated and no built executable was run. The earlier
  pre-boundary pass remains the completed all-seven-target build; macOS runtime
  and the final smoke run remain integration validation for a platform/session
  where process launch is allowed, not missing build wiring.

## Dependencies and parallelism

Best after item 15 and current SatView file churn. A build-system agent can create the framework; module owners can migrate source lists after it is stable.

<model>GPT-5 Codex</model>
