# Extract Megacity model, layout, grid, and routing library

**Type:** refactor
**Priority:** P1 / sequence 05
**Raised by:** Claude and GPT/Codex
**Consensus:** `plans/reviews/review-refactor-consensus.md`, Accepted 6

## Goal

Create one CPU-only `draxul-megacity-model` static library with focused internal
model/layout/grid/routing implementation files and direct deterministic tests.

## Boundary verification

- [ ] Classify every record/function in `semantic_city_layout.h/.cpp` as semantic
  model, spatial layout, grid, routing, or downstream scene/presentation behavior.
- [ ] Inventory all production/test consumers and required public value types.
- [ ] Identify the subset of `MegaCityCodeConfig` actually consumed by model/layout/routing.
- [ ] Define deterministic ordering, stable identity, route, and geometry-index invariants.
- [ ] Confirm host worker cancellation/publication stays outside the proposed library.

## Implementation and migration

- [ ] Introduce a narrow `SemanticCityLayoutOptions` value and an adapter from `MegaCityCodeConfig`.
- [ ] Add module-owned public model/layout/grid/route headers with immutable records/functions.
- [ ] Create `draxul-megacity-model` initially around the existing single implementation TU.
- [ ] Link `draxul-megacity` and focused tests directly to the new target.
- [ ] Split implementation into semantic model, spatial layout, city grid, and routing TUs.
- [ ] Keep pathfinding queues, lot grids, simplification, and helper algorithms private.
- [ ] Narrow `draxul-megacity-test-internals` after tests stop needing the broad product target.

## Unit tests

- [ ] Move semantic projection tests to direct model-target linkage.
- [ ] Pin deterministic lot/module/city placement and stable identity.
- [ ] Pin grid rasterization and occupied/sidewalk/road classification.
- [ ] Pin route ports, pathfinding, duplicate-name identity, and segment conversion.
- [ ] Build `draxul-megacity-model` and `draxul-test-megacity`; run CTest label `megacity`.
- [ ] Add a public-header/link-isolation consumer for the model target.

## Cross-platform validation

- [ ] Configure/build MegaCity ON and OFF on Windows and macOS.
- [ ] Confirm CPU outputs and ordering are identical for Vulkan and Metal consumers.
- [ ] Preserve 16-bit downstream `GeometryMesh` index assumptions.
- [ ] Run the existing MegaCity host/scene tests and launch the host on an available backend.
- [ ] Record other-platform runtime validation if only one backend is available.

## Agent documentation and tooling

- [ ] Update `docs/module-map.md` and `modules/megacity/AGENTS.md` with the new direction.
- [ ] Document that model code is synchronous, deterministic, backend-neutral, and host-thread agnostic.
- [ ] Update focused Megacity validation commands to current target/CTest label names.

## Acceptance criteria

- [ ] `draxul-megacity-model` has no host, ImGui, SDL, font, Vulkan, or Metal dependency.
- [ ] Model/layout/grid/routing tests link without the broad Megacity product/renderer closure.
- [ ] One static link boundary is used; algorithms are not fragmented into micro-libraries.
- [ ] Semantic layout, routing, host behavior, and rendered scene inputs remain equivalent.
- [ ] Focused/full tests, optional ON/OFF builds, and smoke pass.

## Dependencies and ownership

Depends on `kanban/pending/00 internal-target-build-policy -refactor.md`. One
Megacity model owner freezes the public records/options and CMake target. Layout
and routing TU/test moves may then proceed independently without touching
renderer backends or host lifecycle code.
