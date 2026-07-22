# Extract SatView scene composition and host interaction collaborators

**Type:** refactor
**Priority:** P1 / sequence 06
**Raised by:** Claude and GPT/Codex
**Consensus:** `plans/reviews/review-refactor-consensus.md`, Accepted 7

## Goal

Move backend-neutral scene composition into the existing scene target and split
view/selection and ImGui responsibilities inside the host target, leaving
`SatViewHost` as lifecycle/frame orchestration.

## Boundary verification

- [ ] Classify the pure helpers, `draw()` assembly, config/dirty state, picking,
  camera transitions, panels, object tree, and service/worker lifetime in `satview_host.cpp`.
- [ ] Inventory existing scene POD contracts and renderer consumption on Vulkan/Metal.
- [ ] Record dirty-buffer revisions, generation checks, visible-radius, and selection invariants.
- [ ] Record offline fixture seams for fake clock, transport, renderer, and callbacks.
- [ ] Confirm the proposal is narrower than
  `kanban/done/26 satview-library-boundaries -refactor.md`.

## Implementation and migration

- [ ] Add `SatViewSceneComposer` request/result values to `draxul-satview-scene`.
- [ ] Move pure track/marker/color/filter/radius helpers with output-equivalence tests.
- [ ] Replace direct `draw()` vector assembly with one composer request/result incrementally.
- [ ] Add a private view/selection controller for POV, map/ground interactions, and selection transitions.
- [ ] Move ImGui windows/object-tree methods into private host panel TUs one window at a time.
- [ ] Keep services, worker publication, renderer attachment, frame scheduling, and dirty orchestration in `SatViewHost`.
- [ ] Do not add another static library or change the renderer scene ABI.

## Unit tests

- [ ] Add device-free composer tests for track/marker limits, filters, selected emphasis, and visible bounds.
- [ ] Pin composer dirty revisions/generation behavior and projection variants.
- [ ] Add controller tests for POV changes, map dragging, ground entry, and selection/clear transitions.
- [ ] Retain and extend the offline host smoke fixture for integrated lifecycle/draw behavior.
- [ ] Build `draxul-satview-scene` and `draxul-test-satview`; run CTest label `satview`.

## Cross-platform validation

- [ ] Configure/build SatView ON and OFF on Windows and macOS.
- [ ] Verify Vulkan and Metal consume the unchanged scene records and revision semantics.
- [ ] Inspect/build both backends after every scene-contract-adjacent change.
- [ ] Ensure composer/controller contain no HTTP, ImGui, SDL, Vulkan, Metal, or GPU-resource ownership.
- [ ] Run the host on an available backend and record the other backend's runtime status.

## Agent documentation and tooling

- [ ] Update `docs/module-map.md` and the new SatView nested guide with collaborator ownership.
- [ ] Document the immutable composer request/result and main-thread publication rules.
- [ ] Ensure `python do.py test --label satview` remains the narrow validation entry point.

## Acceptance criteria

- [ ] Scene construction and interaction tests run without a GPU, window, network, or system clock.
- [ ] `SatViewHost` retains one owner for service lifetime, frame scheduling, and publication.
- [ ] Renderer ABI, visuals, config persistence, and dirty-frame behavior are unchanged.
- [ ] Independent scene, interaction, and panel ownership lanes no longer require editing one giant TU.
- [ ] Focused/full tests, optional ON/OFF builds, and smoke pass.

## Dependencies and ownership

Depends on `kanban/pending/00 internal-target-build-policy -refactor.md`. One
SatView owner freezes request/result and state ownership. Composer tests and panel
TU moves may then be independent; renderer backend files remain single-owner and
out of scope.
