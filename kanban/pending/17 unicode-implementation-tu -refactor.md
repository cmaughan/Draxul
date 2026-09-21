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

- [ ] Run Unicode, grid, UI-event, font, terminal, and overlay suites.
- [x] Add Unicode use to types link isolation.
- [ ] Benchmark hot cell-width/decoding paths before and after.

## Cross-platform validation

- [ ] Validate MSVC and Apple Clang behavior.
- [x] Pin malformed UTF-8, ambiguous width, emoji/ZWJ/modifier, regional-pair, and Indic cases.

## Agent documentation/tooling

- [x] Document that Unicode remains owned by `draxul-types`.
- [x] Update the target source inventory.

## Acceptance criteria

- [x] The public header contains declarations and only justified inline code.
- [x] Callers and behavior are unchanged.
- [ ] No measurable rendering regression is introduced.
- [ ] Focused/full tests pass.
