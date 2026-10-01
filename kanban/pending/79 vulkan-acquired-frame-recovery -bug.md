# Recover failed frames after image acquisition

**Summary:** Recover a drawing surface after frame preparation fails so rendering can safely continue.

**Priority:** 85  
**Severity:** MEDIUM  
**Source:** `libs/draxul-renderer/src/vulkan/vk_renderer.cpp`

**Evidence and trigger:** B31; command preparation and atlas-recording failures return after acquisition without retiring the image or recovering synchronization.

- [ ] **Investigate:** Inventory every failure after successful acquisition and existing abort/recreation behavior.
- [ ] **Fix:** Route those failures through synchronization recovery and image retirement or swapchain recreation.
- [ ] **Acceptance:** Injected post-acquisition failures permit the next frame without reusing an outstanding acquisition semaphore or leaking acquired images.
- [ ] **Validation:** Run core aggregate tests, Vulkan validation and relevant render checks, and same-cache smoke.
