# Enforce product dependency direction

**Type:** refactor  
**Priority:** P2  
**Raised by:** Claude  
**Depends on:** pending `00`

## Boundary verification

- [ ] Inventory every internal target and assign core/Markdown/Kanban/Megacity/SatView/Score family ownership.
- [ ] Record allowed integration edges and the executable exemption.
- [ ] Verify current direct and interface link properties.

## Implementation and migration

- [ ] Reuse pending `00` target metadata to record product family.
- [ ] Extend `CheckDependencyBoundaries.cmake`.
- [ ] Reject core-to-product links.
- [ ] Reject cross-product links.
- [ ] Guard checks for disabled optional targets.
- [ ] Keep error messages specific to the violated rule.

## Unit/configure tests

- [ ] Add negative fixtures for core→product and product→other-product edges.
- [ ] Add positive executable-integration and product→core fixtures.
- [ ] Configure all-optionals-off and each optional individually enabled.

## Cross-platform validation

- [ ] Exercise MSVC and Apple generators.
- [ ] Confirm checks are configure-only and introduce no backend behavior.

## Agent documentation/tooling

- [ ] Document family metadata beside the pending `00` helper.
- [ ] Update module-map dependency rules without duplicating target lists.

## Acceptance criteria

- [ ] Prohibited direct/interface edges fail configuration with actionable errors.
- [ ] Optional-disabled configurations remain valid.
- [ ] The executable remains the sole product integration root.
- [ ] Current supported target graph configures on both platforms.
