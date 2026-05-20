# Bug: ImageResource::size stores pixel width instead of byte size

**Severity**: HIGH
**File**: `libs/draxul-megacity/src/megacity_render_vk.cpp:485`
**Source**: review-bugs-consensus.md (H2)

## Description

In `create_sampled_image`, after creating the image, sampler, and view:
```cpp
image.size = width;   // WRONG — stores pixel width, not byte size
```
`ImageResource::size` is expected to be the byte size of the image data (width × height × bytes_per_pixel). Any consumer of `.size` for transfer sizing, staging buffer allocation, or flush bounds operates with a value ~4× too small, silently under-transferring or misvalidating uploads.

## Trigger Scenario

Any code path that reads `ImageResource::size` to determine how many bytes to copy, upload, or flush. This affects texture uploads in the Megacity Vulkan renderer.

## Fix Strategy

- [x] Replace line 485 with:
  ```cpp
  image.size = static_cast<VkDeviceSize>(width) * height * 4;
  ```
- [x] Search for all call sites that read `.size` and verify they now receive the correct byte count
- [x] Fix H3 (resource leak in `create_sampled_image`) and H4 (upload_mesh empty guard) in the same file pass — see work items 54 and 55

## Acceptance Criteria

- [x] `ImageResource::size` equals `width * height * 4` for all created images
- [x] Megacity textures upload and display correctly after the fix
- [x] No Vulkan validation layer errors about incorrect transfer sizes
