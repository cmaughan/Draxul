# Split Chrome layout, rendering, and editing

**Type:** refactor
**Priority:** 23
**Raised by:** GPT/Codex, Gemini; detailed by GPT/Codex

## Goal

`ChromeHost::draw()` mixes tab/pane/resource/weather layout, hit regions, NanoVG shapes, grid text, rename editing, and coordinate conversion. Extract behavioral components without changing visuals.

## Implementation plan

- [ ] Land Unicode item 08 and allocation tests item 20 first.
- [ ] Capture current tab/status/weather geometry in pure tests across DPI, resize, zoom, one/many workspaces, and rename state.
- [ ] Extract pure `ChromeLayout` input/output structs producing rectangles, text runs, clipping, and stable hit-region ids.
- [ ] Extract `RenameEditor` state transitions/caret/text operations with no renderer dependency.
- [ ] Extract `ChromeVectorPass` for NanoVG shapes and `ChromeTextLayer` for grid-backed text.
- [ ] Make `ChromeHost` assemble the layout and passes, forward input/editing, and expose hit-test results.
- [ ] Remove unused `Deps` fields after verifying no hidden tests rely on them.
- [ ] Centralize logical/physical/cell conversion in the layout input boundary.

## Tests and acceptance

- [ ] Golden-structure tests compare layout/hit ids, not screenshots, for key states.
- [ ] Existing chrome/rename/hit-test tests and render references remain stable.
- [ ] `ChromeHost::draw()` becomes orchestration-sized and contains no rename state machine.
- [ ] Build, `ctest`, render scenarios, and smoke pass on both backend code paths.

## Dependencies and parallelism

Depends on 08 and 20. Give one UI sub-agent exclusive ownership of `chrome_host.*`; pure layout tests can be prepared separately first.

<model>GPT-5 Codex</model>
