# Move Unicode implementation out of the public header

**Type:** refactor  
**Priority:** P2  
**Raised by:** Codex

## Boundary verification

- [x] Inventory all 22 direct consumers and required API declarations.
- [x] Classify tables, high-level helpers, and hot scalar helpers.
- [x] Record existing malformed-input, width, emoji, and cluster behavior.

## Implementation and migration

- [x] Add `libs/draxul-types/src/unicode.cpp`.
- [x] Move range tables and classification logic.
- [x] Move clustering and vector-producing helpers.
- [x] Retain only measured/trivial inline helpers.
- [x] Preserve all names, signatures, defaults, and replacement behavior.

## Unit tests

- [x] Run Unicode, grid, UI-event, font, terminal, and overlay suites.
- [x] Add Unicode use to types link isolation.
- [x] Benchmark hot cell-width/decoding paths before and after.

## Cross-platform validation

- [x] Validate Apple Clang behavior.
- [ ] Validate MSVC behavior on Windows.
- [x] Pin malformed UTF-8, ambiguous width, emoji/ZWJ/modifier, regional-pair, and Indic cases.

## Agent documentation/tooling

- [x] Document that Unicode remains owned by `draxul-types`.
- [x] Update the target source inventory.

## Acceptance criteria

- [x] The public header contains declarations and only justified inline code.
- [x] Callers and behavior are unchanged.
- [x] No measurable Unicode width/decoding hot-path regression is present on Apple Clang.
- [x] Focused and full macOS tests pass.
- [ ] Focused and full Windows tests pass under MSVC.

## Validation evidence

- macOS 26.5.2, Apple M5, Apple Clang 21.0.0: Unicode/Neovim width-oracle coverage passed 2 cases with 36 assertions; UI-event coverage passed 22 cases with 160 assertions.
- Grid, `CellText`, font, terminal-core, VT parser, and core overlay consumers passed 163 cases with 23,126 assertions; overlay Unicode/render-contract coverage passed 6 cases with 40 assertions; overlay allocation-failure coverage passed 7 cases with 58 assertions; app-shell Unicode layout passed 1 case with 8 assertions.
- A temporary runtime-corpus microbenchmark compiled the pre-extraction header and current translation unit with the same `-O3 -DNDEBUG` Apple Clang settings. Across nine samples, median decode time was 6.242 ns per corpus entry before and 6.278 ns after (+0.6%); median cluster-width time was 26.860 ns before and 26.800 ns after (-0.2%). These differences are within sample noise and support only the stated hot-path claim, not a broader frame-time claim.
- `python3 do.py test debug` passed 24/24 CTest entries; `python3 do.py smoke debug --skip-build` passed with the Metal renderer.
