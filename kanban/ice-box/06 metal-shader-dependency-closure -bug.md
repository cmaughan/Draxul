# Complete Metal shader dependency tracking

**Type:** bug
**Priority:** 06
**Raised by:** Claude, GPT/Codex

## Problem

`cmake/CompileShaders_Metal.cmake` compiles `grid.metal` with shared headers but lists only `decoration_constants_shared.h` as a dependency. Editing `quad_offsets_shared.h` can leave `grid.metallib` stale. Other Metal targets use similarly manual dependency lists.

## Implementation plan

- [ ] Audit every `#include` reachable from each `.metal` entry point.
- [ ] Add the missing grid dependency immediately.
- [ ] Introduce one CMake helper for Metal compile/link targets that accepts an explicit entry point and dependency closure.
- [ ] Reuse shared dependency sets for grid/markdown/product shaders without broadly depending on unrelated shader files.
- [ ] Keep module shader targets gated by their feature options.
- [ ] Add a configure-time check that every repository-local Metal include is declared for its entry point.

## Tests

- [ ] Touch each shared header in a throwaway build test and verify the affected `.air`/`.metallib` rebuilds.
- [ ] Verify unrelated shader libraries do not rebuild.
- [ ] Configure with MegaCity/SatView enabled and disabled.

## Acceptance criteria

- [ ] Changing `quad_offsets_shared.h` rebuilds `grid.metallib`.
- [ ] All local Metal includes have tracked CMake dependencies.
- [ ] macOS app/test build and smoke pass.

## Dependencies and parallelism

Independent bug; `kanban/ice-box/19 shader-abi-parity -test.md` validates ABI rather
than build invalidation.

<model>GPT-5 Codex</model>
