# Shared private GPU resource helpers

**Type:** refactor
**Priority:** 28
**Raised by:** Claude

## Goal

Remove repeated, error-prone buffer/image/shader/staging/attachment setup across core, Markdown, MegaCity, and SatView renderers while keeping backend APIs private.

## Implementation plan

- [x] Fix NanoVG synchronization in item 04 before generalizing any resource code.
- [x] Inventory exact repeated operations and their lifetime/synchronization differences; start with Vulkan because three copies are explicit.
- [x] Expand backend-private `vk_resource_helpers.h` with RAII buffer/image owners, checked shader load, staging upload, layout transition, and attachment creation.
- [x] Require caller-supplied usage, memory policy, final layout, debug name, and lifetime scope; do not hide synchronization decisions behind defaults.
- [x] Migrate one small renderer (Markdown) first, then SatView, then selected MegaCity families. (Markdown is migrated; SatView attachment, shader, staging, and transition families are migrated; MegaCity persistent/dynamic meshes, frame/transient buffers, staging, sampled images, shaders, 2D/cube attachments, views, and caller-authored transitions use the private helpers.)
- [x] Evaluate equivalent Metal helpers only for demonstrated duplication; preserve native ownership idioms. (Metal uses ARC/native ownership and does not duplicate the VMA partial-allocation paths; no artificial cross-backend API added.)
- [x] Keep helpers in private renderer/module implementation targets, not public `draxul-renderer` headers.

### Vulkan lifetime and synchronization inventory

- Markdown persistent atlas images are retired only after `vkDeviceWaitIdle`; its frame-owned mapped buffers are reused and grown transactionally.
- SatView immediate texture staging is upload-scoped and released after queue-idle, while HDR attachments are frame-scoped and retired after device-idle resize.
- MegaCity keeps persistent mesh/material/label resources, frame-owned uniforms and tooltip resources, and explicit retired-resource queues; these must not be collapsed into one destruction policy.
- Image barriers remain caller-authored with explicit layouts, access masks, stages, and subresource ranges. The upload helper records only the transitions and copy supplied by its caller.
- MegaCity mip generation retains its specialized per-level barriers and blits; these encode a different subresource state machine and are deliberately not hidden behind the simple upload transition helper.

## Tests and acceptance

- [x] Add failure-injection tests for partial allocation/upload cleanup where seams permit.
- [ ] Run Vulkan validation for every migrated caller and existing render snapshots. (Outstanding: this requires launching renderer/test executables, explicitly prohibited for the current task; build-only coverage is recorded below.)
- [x] Resource ownership/retirement semantics remain explicit at call sites.
- [x] Code reduction does not merge unrelated pass policy.

Build-only validation on Windows Debug: `draxul-vulkan-resources`, `draxul-renderer-vulkan`,
`draxul-markdown-host`, `draxul-satview-renderer`, and `draxul-codeviz-renderer` compile.
The expanded MegaCity buffer/image/attachment migration was revalidated by rebuilding
`draxul-vulkan-resources` and `draxul-codeviz-renderer` after a fresh CMake generation.
`vk_resource_helpers_tests.cpp` also compiles in `draxul-test-core`; the aggregate target
was not run and did not finish linking because concurrent foundation refactors temporarily
removed unrelated public include paths. No application, helper, or test executable was launched.

The 2026-07-21 audit added Windows Release build-only coverage: the SatView
Vulkan renderer/host built with the helpers enabled, and a Release `draxul`
build linked the migrated core/Markdown path. The card remains pending solely
for live Vulkan validation and render-snapshot execution across every migrated
caller; those gates require launching renderer/test executables and were not
run in this task.

## Dependencies and parallelism

Depends on item 04; supports item 27 and later SatView renderer cleanup. One Vulkan specialist should own helper semantics while module agents migrate callers sequentially.

<model>GPT-5 Codex</model>
