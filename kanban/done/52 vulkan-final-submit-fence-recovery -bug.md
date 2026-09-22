# Recover frame ownership after final submission failure

**Severity:** HIGH  
**Reported by:** Claude Fable 5.1

`libs/draxul-renderer/src/vulkan/vk_renderer.cpp:1345` resets the frame fence before submission, but failure at `:1349` leaves it unsignaled and associated with the image. A subsequent `begin_frame()` at `:884` can wait for work never submitted.

**Investigation**

- [x] Inject final-submit failure and inspect fence, image, semaphore, and frame ownership.

**Fix strategy**

- [x] Enter a defined renderer-failure or recovery state that never waits on nonexistent work.
- [x] Repair or retire associated synchronization objects safely; do not reset a fence after submitting it.

**Implementation note (2026-09-21):** The image-to-fence association now occurs
only after final submit succeeds. A failed submission retires the unsignaled
frame fence and image-available semaphore after the device is idle, replaces
them with a signaled fence and fresh semaphore, and aborts the frame before the
next acquire. Focused Vulkan source-contract tests passed on macOS (75 cases,
635 assertions). Failure injection and Windows Vulkan validation remain pending.

**Coverage note (2026-09-22):** The Windows white-box test now injects final
submit failure after fence reset and verifies cleared image association, retired
acquire semaphore, a signaled replacement frame fence, and an inactive frame.
Runtime validation-layer/render coverage remains pending.

**Acceptance criteria**

- [x] Submission failure cannot cause indefinite fence reuse or invalid synchronization.
- [x] Coordinate with `kanban/done/21 vulkan-chunk-flush-failure-state -bug.md`.

## Windows closeout (2026-09-22)

Final-submit failure injection passed in the final 48/48 products aggregate;
the Vulkan render inventory and same-cache smoke passed without a fence hang.
