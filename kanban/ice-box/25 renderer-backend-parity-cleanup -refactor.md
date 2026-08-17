# 25 renderer-backend-residual-cleanup -refactor

**Priority:** Medium
**Type:** Refactor

## Current State

Most of the original Metal/Vulkan parity cleanup has landed. Both backends now share the same public render-pass shape, frame-buffered grid packing, staged atlas upload model, ImGui hosting, frame capture, and depth-backed 3D paths. The stale Vulkan single-handle renderer state was removed, and Vulkan atlas upload waits were narrowed.

This card tracks only the residual cleanup that is still useful.

## Remaining Work

### 1. Confirm Metal render parity

- [ ] Run the current render snapshot suite on macOS Metal.
- [ ] If `panel-view` or another scenario still drifts, isolate whether the difference is grid packing, panel/overlay composition, font metrics, or capture timing.
- [ ] Fix only the minimal source of the drift.

### 2. Split the largest Metal renderer internals only where it pays off

- [ ] Review `libs/draxul-renderer/src/metal/metal_renderer.mm` for the largest remaining clusters.
- [ ] Extract helper types/functions for frame capture and frame submission/drawable lifecycle if doing so reduces local complexity.
- [ ] Keep the public renderer boundary unchanged.

## Acceptance Criteria

- Metal and Vulkan pass the supported baseline render scenarios.
- Remaining backend differences are documented as intentional API/platform differences.
- `metal_renderer.mm` is easier to navigate without introducing public backend leakage.
- Product geometry and mesh-storage policy remain in the owning plugin repositories.

Cross-platform reference promotion is tracked separately in
`kanban/ice-box/36 cross-platform-render-reference-promotion -feature.md`.
