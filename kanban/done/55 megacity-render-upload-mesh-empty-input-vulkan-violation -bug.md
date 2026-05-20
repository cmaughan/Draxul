# Bug: upload_mesh passes size=0 to vkCreateBuffer — Vulkan spec violation

**Severity**: HIGH
**File**: `libs/draxul-megacity/src/megacity_render_vk.cpp:299`
**Source**: review-bugs-consensus.md (H4)

## Description

`upload_mesh` does not guard against an empty `MeshData`. When `mesh.vertices` or `mesh.indices` is empty, `vertex_bytes` or `index_bytes` is 0. `create_mapped_buffer` is called with `size == 0`, which calls `vkCreateBuffer` with `VkBufferCreateInfo::size == 0`. The Vulkan spec forbids `size == 0` for buffer creation — validation layers will fire a `VUID-VkBufferCreateInfo-size-00912` error and behaviour is undefined.

`stream_transient_mesh` (a sibling function in the same file) already has an empty-guard — `upload_mesh` is missing the equivalent.

## Trigger Scenario

Any call to `upload_mesh` with an empty city layer or mesh (possible during incremental city data loading or empty scene initialization).

## Fix Strategy

- [x] Add at the top of `upload_mesh`:
  ```cpp
  if (mesh.vertices.empty() || mesh.indices.empty())
  {
      gpu_mesh = {};
      return true;
  }
  ```
- [x] Fix H2 (wrong image size) and H3 (resource leak) in the same file pass — see work items 53 and 54

## Acceptance Criteria

- [x] Calling `upload_mesh` with empty vertex/index arrays succeeds without Vulkan validation errors
- [x] Normal non-empty mesh uploads are unaffected
