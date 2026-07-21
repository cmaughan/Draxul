# Split Chrome layout, rendering, and editing

**Type:** refactor
**Priority:** 23
**Raised by:** GPT/Codex, Gemini; detailed by GPT/Codex

## Goal

`ChromeHost::draw()` mixes tab/pane/resource/weather layout, hit regions, NanoVG shapes, grid text, rename editing, and coordinate conversion. Extract behavioral components without changing visuals.

## Implementation plan

- [x] Allocation-failure tests are done (`done/20 overlay-allocation-failure -test.md`); the earlier "Unicode item 08" prerequisite no longer maps to a current card (numbers were reused), so re-confirm whether any Unicode-hardening blocker remains before starting.
- [x] Capture current tab/status/weather geometry in pure tests across DPI, resize/zoom cell metrics, one/many workspaces, and rename state.
- [x] Extract pure `ChromeLayout` input/output structs producing rectangles, text runs, clipping, and stable hit-region ids.
- [x] Extract `RenameEditor` state transitions/caret/text operations with no renderer dependency.
- [x] Extract `ChromeVectorPass` for NanoVG shapes and `ChromeTextLayer` for grid-backed text.
- [x] Make `ChromeHost` assemble the layout and passes, forward input/editing, and expose hit-test results.
- [x] Remove unused `Deps` fields after verifying no hidden tests rely on them. (Removed the seven former HostManager-forwarding dependencies; App now owns that composition directly.)
- [x] Centralize logical/physical/cell conversion in the layout input boundary.

## Tests and acceptance

- [x] Golden-structure tests compare layout/hit ids, not screenshots, for key states.
- [ ] Existing chrome/rename/hit-test tests and render references remain stable.
- [x] `ChromeHost::draw()` becomes orchestration-sized and contains no rename state machine.
- [ ] Build, `ctest`, render scenarios, and smoke pass on both backend code paths.

### Validation status (2026-07-21)

Windows Release build-only coverage is green: `draxul-test-app` compiled and
linked the chrome layout/rename test sources, and a Release `draxul` build also
linked successfully. No test, render, helper, or application executable was
launched. This card remains pending for execution of the existing chrome,
rename, hit-test, render-reference, `ctest`, and smoke gates, plus macOS/Metal
validation.

## Dependencies and parallelism

Allocation tests (`done/20 overlay-allocation-failure -test.md`) are complete; the earlier "item 08" Unicode prerequisite is stale (no current card carries that number/topic). Give one UI sub-agent exclusive ownership of `chrome_host.*`; pure layout tests can be prepared separately first.

<model>GPT-5 Codex</model>
