# Move Unicode implementation out of the public header

**Type:** refactor  
**Priority:** P2  
**Raised by:** Codex

## Boundary verification

- [ ] Inventory all 22 direct consumers and required API declarations.
- [ ] Classify tables, high-level helpers, and hot scalar helpers.
- [ ] Record existing malformed-input, width, emoji, and cluster behavior.

## Implementation and migration

- [ ] Add `libs/draxul-types/src/unicode.cpp`.
- [ ] Move range tables and classification logic.
- [ ] Move clustering and vector-producing helpers.
- [ ] Retain only measured/trivial inline helpers.
- [ ] Preserve all names, signatures, defaults, and replacement behavior.

## Unit tests

- [ ] Run Unicode, grid, UI-event, font, terminal, and overlay suites.
- [ ] Add Unicode use to types link isolation.
- [ ] Benchmark hot cell-width/decoding paths before and after.

## Cross-platform validation

- [ ] Validate MSVC and Apple Clang behavior.
- [ ] Pin malformed UTF-8, ambiguous width, emoji/ZWJ/modifier, regional-pair, and Indic cases.

## Agent documentation/tooling

- [ ] Document that Unicode remains owned by `draxul-types`.
- [ ] Update the target source inventory.

## Acceptance criteria

- [ ] The public header contains declarations and only justified inline code.
- [ ] Callers and behavior are unchanged.
- [ ] No measurable rendering regression is introduced.
- [ ] Focused/full tests pass.
