# Bug: Partial sync object creation leaks semaphores/fences on Vulkan init failure

**Severity**: MEDIUM
**File**: `libs/draxul-renderer/src/vulkan/vk_renderer.cpp:305–312`
**Source**: review-bugs-consensus.md (M1)

## Description

`create_sync_objects` creates `MAX_FRAMES_IN_FLIGHT` semaphore pairs and fences in a loop. If any `vkCreate*` call fails mid-loop, the function returns `false` immediately. All objects created in prior iterations are already stored in the vectors (`image_available_sem_`, `render_finished_sem_`, `in_flight_fences_`) but are never destroyed. The vectors are not cleared until the next `initialize()` call, which never happens on an init failure path.

## Trigger Scenario

`vkCreateSemaphore` or `vkCreateFence` fails after the first iteration (extreme driver resource exhaustion or fault injection).

## Fix Strategy

- [x] On any failure, iterate the vectors up to the current index and call `vkDestroySemaphore`/`vkDestroyFence` on all non-null handles before returning false
- [x] Set handles to `VK_NULL_HANDLE` after destroying to avoid double-destroy in the normal `shutdown()` path

## Acceptance Criteria

- [x] Injecting failure at `vkCreateFence` for `i == 1` (second iteration) leaves no leaked handles
- [x] Normal initialization with `MAX_FRAMES_IN_FLIGHT` successes is unaffected
