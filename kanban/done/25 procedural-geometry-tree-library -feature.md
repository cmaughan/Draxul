# Feature: Procedural Geometry Library And Tree Generator

**Type:** feature
**Priority:** 25
**Source:** Megacity planning

## Overview

Megacity currently relies on a small fixed mesh helper for cubes, signs, roads, and grids. That is not the right place for recursive procedural geometry such as trees. We need a new static library, `draxul-geometry`, that owns renderer-agnostic CPU mesh generation.

The first generator is `DraxulTree`: a bark/trunk/branch mesh generator with positions, normals, tangents, colors, UVs, and indices. The scaffold phase can start with a valid trunk-only mesh and age-based defaults; later phases add recursive offshoot branches, attachment stitching, and Megacity integration.

## Implementation plan

### Phase 1: Library scaffold

- [x] Add `libs/draxul-geometry` as a new static library in CMake.
- [x] Add public geometry mesh types (`GeometryVertex`, `GeometryMesh`).
- [x] Add public tree generator API (`DraxulTreeParams`, `make_tree_params_from_age()`, `generate_draxul_tree()`).
- [x] Make the shared geometry vertex format compatible with Megacity’s current first five vertex attributes and add tangent data at the end.
- [x] Link the new library into `draxul-megacity`.
- [x] Add unit tests for the scaffold API and deterministic output.
- [x] Add a shared unit cube generator to `draxul-geometry`.

### Phase 2: Trunk-first generator

- [x] Generate a tapered trunk mesh from rings with:
  - [x] positions
  - [x] normals
  - [x] tangents
  - [x] colors
  - [x] UVs
  - [x] indices
- [x] Add top/bottom caps.
- [x] Add bounded age-based defaults that map saplings to mature trunks.

### Phase 3: Branch descriptor system

- [x] Introduce explicit branch descriptors and per-branch ring records.
- [x] Separate descriptor generation from mesh emission.
- [x] Keep generation deterministic via stable seed hashing.

### Phase 4: Recursive branch growth

- [x] Generate child branch counts from configurable ranges.
- [x] Spawn child branches from local parent-ring patches.
- [x] Scale child length/radius relative to the parent branch.
- [x] Add branch curvature and upward/outward bias controls.

### Phase 5: Branch attachment stitching

- [x] Build local stitched junctions between parent branch patches and child root rings.
- [x] Smooth normals near the branch roots so junctions read as growth instead of pipe intersections.
- [x] Keep UV and tangent continuity consistent enough for bark materials.

### Phase 6: Megacity rendering integration

- [x] Choose the first integration model:
  - [x] prototype shared tree meshes
  - [x] or dynamic procedural mesh registration/cache
- [x] Remove shader-generated UV/tangent conventions from Megacity materials and rely on mesh-provided data instead.
- [x] Replace placeholder cube trees in Megacity with generated tree meshes.
- [x] Extend Megacity vertex/render paths if tangents or custom mesh handles require it.

## Acceptance criteria

- [x] The repo builds with a new `draxul-geometry` static library.
- [x] The tree generator API is testable without a renderer.
- [x] A fixed seed produces deterministic trunk geometry.
- [x] Recursive branch generation produces obviously tree-like forms rather than just cylinders.
- [x] Megacity can render at least one generated tree mesh instead of placeholder cube trees.

## Interdependencies

- `plans/design/draxul_geometry.md` is the design note for the library boundary and growth strategy.
- Megacity renderer work will likely need a procedural mesh path or a shared generated-mesh cache later.

---
*Filed by `codex` · 2026-03-27*
