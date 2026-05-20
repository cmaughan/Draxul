# Bug: GPU resource leak on partial failure in create_sampled_image

**Severity**: HIGH
**File**: `libs/draxul-megacity/src/megacity_render_vk.cpp:450–481`
**Source**: review-bugs-consensus.md (H3)

## Description

`create_sampled_image` allocates a VMA image + allocation, a `VkImageView`, and a `VkSampler` in sequence. The failure handling is incomplete:

- If `vkCreateImageView` fails (line 460), the VMA image and allocation are never freed.
- If `vkCreateSampler` fails (line 480), the image, allocation, and view are never freed.

Each failure under GPU memory pressure leaks allocated GPU memory permanently.

## Trigger Scenario

GPU memory exhaustion or transient driver error during Megacity initialization, reload, or texture creation.

## Fix Strategy

- [x] On `vkCreateImageView` failure: call `vmaDestroyImage(allocator, image.image, image.allocation)` before returning false
- [x] On `vkCreateSampler` failure: destroy image view then VMA image/allocation before returning false
- [x] Consider a RAII helper or explicit cleanup labels to keep the failure paths readable
- [x] Fix H2 (wrong size) and H4 (upload_mesh guard) in the same file pass — see work items 53 and 55

## Acceptance Criteria

- [x] Injecting failure at each `vkCreate*` call results in zero leaked handles (verified with VMA leak detection or a mock)
- [x] No Vulkan validation layer errors about unreleased resources after a failed initialization
