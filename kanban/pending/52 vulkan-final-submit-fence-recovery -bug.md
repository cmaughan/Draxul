# Recover frame ownership after final submission failure

**Severity:** HIGH  
**Reported by:** Claude Fable 5.1

`libs/draxul-renderer/src/vulkan/vk_renderer.cpp:1345` resets the frame fence before submission, but failure at `:1349` leaves it unsignaled and associated with the image. A subsequent `begin_frame()` at `:884` can wait for work never submitted.

**Investigation**

- [ ] Inject final-submit failure and inspect fence, image, semaphore, and frame ownership.

**Fix strategy**

- [ ] Enter a defined renderer-failure or recovery state that never waits on nonexistent work.
- [ ] Repair or retire associated synchronization objects safely; do not reset a fence after submitting it.

**Acceptance criteria**

- [ ] Submission failure cannot cause indefinite fence reuse or invalid synchronization.
- [ ] Coordinate with `kanban/pending/21 vulkan-chunk-flush-failure-state -bug.md`.
