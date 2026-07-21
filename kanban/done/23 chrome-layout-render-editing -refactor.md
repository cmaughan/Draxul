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
- [x] Existing chrome/rename/hit-test tests and render references remain stable.
- [x] `ChromeHost::draw()` becomes orchestration-sized and contains no rename state machine.
- [x] Build, `ctest`, render scenarios, and smoke pass on both backend code paths. — macOS/Metal validated (ctest 22/22, 5 render scenarios, smoke green 2026-07-21); Vulkan via CI.

### Validation status (2026-07-21)

Windows automated coverage is green for this refactor. `t.bat release` built the
full tree and passed all 22 CTest entries, including app smoke, the app test
shards containing the chrome/rename/hit-test cases, and all five render
snapshots. `t.bat debug` also built successfully; its smoke and all five Vulkan
render snapshots passed under the Debug validation-layer path. The only Debug
suite failure is the unrelated session named-pipe owner/DACL assertion recorded
on item 25. The user also completed a PC visual check with no behavior or visual
regression observed. The cross-backend acceptance item remains open for
macOS/Metal execution.

## Dependencies and parallelism

Allocation tests (`done/20 overlay-allocation-failure -test.md`) are complete; the earlier "item 08" Unicode prerequisite is stale (no current card carries that number/topic). Give one UI sub-agent exclusive ownership of `chrome_host.*`; pure layout tests can be prepared separately first.

<model>GPT-5 Codex</model>

## Verified 2026-07-21

ChromeLayout / RenameEditor / ChromeVectorPass / ChromeTextLayer extracted (app/chrome_layout.*, rename_editor.*, chrome_vector_pass.*, chrome_text_layer.*) with chrome_layout_tests (6 cases). Verified on the current TOT: build clean, ctest 22/22, render scenarios pass unblessed, smoke green. Moved to done; Vulkan backend path via CI.
