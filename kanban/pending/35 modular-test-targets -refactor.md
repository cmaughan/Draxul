# Modularize the test targets

**Type:** refactor
**Priority:** 35
**Raised by:** GPT/Codex

## Goal

The single `draxul-tests` executable links App and every enabled product module, increasing link time and obscuring optional-module coverage. Split by stable target boundary while preserving shared fixtures and simple developer commands.

## Implementation plan

- [ ] Measure current configure/build/link/test times and classify test sources by owning library/module.
- [ ] Create shared test-support target(s) for existing fakes/helpers without duplicating implementations.
- [ ] Introduce focused executables/CTest labels for core, app/session, Markdown/Kanban, MegaCity, SatView, and ScoreView in stages.
- [ ] Link a ScoreView host-focused target to `draxul-scoreview-host` so microphone/NanoVG/host lifecycle tests do not force every core test to take the full product dependency.
- [ ] Link each test target only to the public/internal test interface it needs; remove private `src/` includes by reopening the existing boundary card where required.
- [ ] Keep a top-level `draxul-tests` build target that depends on all enabled test executables for workflow compatibility.
- [ ] Update sanitizer, coverage, RPC-helper dependencies, scripts, and CI discovery.
- [ ] Ensure optional-off configurations omit only their own test targets, including `DRAXUL_ENABLE_SCOREVIEW=OFF`.

## Tests and acceptance

- [ ] Compare test case/tag counts before and after; no case disappears.
- [ ] `ctest -j` can run independent targets concurrently and reports module labels.
- [ ] Windows/macOS presets, sanitizers, coverage, `t.bat`/`t.sh`, and smoke remain usable.
- [ ] Record measured link/test improvement or revert splits that add complexity without benefit.

## Dependencies and parallelism

Best after item 15 and current SatView file churn. A build-system agent can create the framework; module owners can migrate source lists after it is stable.

<model>GPT-5 Codex</model>
