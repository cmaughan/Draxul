# Separate ScoreView keyboard layout from NanoVG replay

**Type:** refactor  
**Priority:** P2  
**Raised by:** Codex  
**Depends on:** coordinate with pending `07`

## Boundary verification

- [ ] Inventory geometry, palette, presentation values, draw callers, and tests.
- [ ] Confirm which overlay code requires palette APIs.
- [ ] Record palette bytes and representative key positions.

## Implementation and migration

- [ ] Add public core `keyboard_layout.h`.
- [ ] Move MIDI bounds, geometry, and palette APIs into it.
- [ ] Move `KeyboardLit` and draw declarations to a host-private header.
- [ ] Update core/host includes.
- [ ] Remove `keyboard_render_nvg.h` without forwarding duplication.
- [ ] Extract keyboard tests from the host-classified composer suite.

## Unit tests

- [ ] Add `scoreview_keyboard_layout_tests.cpp`.
- [ ] Test 88-key bounds, black/white classification, centers, spelling, and palette selection.
- [ ] Keep one host-level draw smoke if useful.
- [ ] Build/run `draxul-test-scoreview` and `draxul-test-scoreview-host`.

## Cross-platform validation

- [ ] Verify NanoVG output and palette bytes on Windows and macOS.
- [ ] Confirm core keyboard tests link no host/NanoVG/SDL/ImGui dependency.

## Agent documentation/tooling

- [ ] Update the Score nested guide/module map under pending `08`.

## Acceptance criteria

- [ ] Core publishes no `NVGcontext` keyboard API.
- [ ] Geometry/palette tests use only `draxul-scoreview`.
- [ ] Visual output is unchanged.
- [ ] Score core/host tests pass.
