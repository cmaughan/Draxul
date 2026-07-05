# Shared private GPU resource helpers

**Type:** refactor
**Priority:** 28
**Raised by:** Claude

## Goal

Remove repeated, error-prone buffer/image/shader/staging/attachment setup across core, Markdown, MegaCity, and SatView renderers while keeping backend APIs private.

## Implementation plan

- [ ] Fix NanoVG synchronization in item 04 before generalizing any resource code.
- [ ] Inventory exact repeated operations and their lifetime/synchronization differences; start with Vulkan because three copies are explicit.
- [ ] Expand backend-private `vk_resource_helpers.h` with RAII buffer/image owners, checked shader load, staging upload, layout transition, and attachment creation.
- [ ] Require caller-supplied usage, memory policy, final layout, debug name, and lifetime scope; do not hide synchronization decisions behind defaults.
- [ ] Migrate one small renderer (Markdown) first, then SatView, then selected MegaCity families.
- [ ] Evaluate equivalent Metal helpers only for demonstrated duplication; preserve native ownership idioms.
- [ ] Keep helpers in private renderer/module implementation targets, not public `draxul-renderer` headers.

## Tests and acceptance

- [ ] Add failure-injection tests for partial allocation/upload cleanup where seams permit.
- [ ] Run Vulkan validation for every migrated caller and existing render snapshots.
- [ ] Resource ownership/retirement semantics remain explicit at call sites.
- [ ] Code reduction does not merge unrelated pass policy.

## Dependencies and parallelism

Depends on item 04; supports item 27 and later SatView renderer cleanup. One Vulkan specialist should own helper semantics while module agents migrate callers sequentially.

<model>GPT-5 Codex</model>
