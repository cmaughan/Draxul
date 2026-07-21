#pragma once

#include <cstddef>
#include <utility>

namespace draxul
{

enum class BufferResizeResult
{
    Unchanged,
    Resized,
    Failed,
};

template <typename BufferHandle, typename AllocationHandle>
struct OwnedMappedBuffer
{
    BufferHandle buffer{};
    AllocationHandle allocation{};
    void* mapped = nullptr;
    size_t size = 0;
};

// Builds a resource off to the side and only publishes it after every creation
// stage succeeds. This is the common failure-safety rule used by the concrete
// Vulkan owners without exposing Vulkan through the public renderer API.
template <typename Resource, typename CreatePrimaryFn, typename CreateDependentFn, typename DestroyFn>
bool create_resource_transactionally(
    Resource& output,
    CreatePrimaryFn&& create_primary,
    CreateDependentFn&& create_dependent,
    DestroyFn&& destroy)
{
    Resource replacement{};
    if (!create_primary(replacement))
        return false;
    if (!create_dependent(replacement))
    {
        destroy(replacement);
        return false;
    }

    output = std::move(replacement);
    return true;
}

template <typename BufferState, typename CreateFn, typename DestroyFn>
BufferResizeResult ensure_buffer_size(BufferState& current, size_t required_size, CreateFn&& create_buffer, DestroyFn&& destroy_buffer)
{
    if (required_size <= current.size)
        return BufferResizeResult::Unchanged;

    BufferState replacement{};
    if (!create_buffer(required_size, replacement))
        return BufferResizeResult::Failed;

    if (current.buffer != BufferState{}.buffer)
        destroy_buffer(current);

    current = std::move(replacement);
    return BufferResizeResult::Resized;
}

} // namespace draxul
