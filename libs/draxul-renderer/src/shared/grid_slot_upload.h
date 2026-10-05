#pragma once

// Per-frame grid slot upload policy — one definition for both renderer backends.
//
// Each grid handle owns one GPU buffer per frame in flight. A resize grows the
// RendererState immediately, but the slot's buffer only grows when the next
// upload for that slot succeeds. Metal used to ignore a failed growth (or a
// failed mapping) and then draw the resized state's instance counts against the
// smaller, stale buffer, so the vertex shader indexed past the end of the
// allocation (kanban 61). The rule here: a slot may only be drawn when this
// frame's upload copied the complete current state into storage large enough
// for every instance the bg/fg draws address. Otherwise the caller skips the
// draw; the state stays dirty and the next frame retries the upload.
//
// Backend-private, like grid_contract.h: kept free of Metal/Vulkan headers so
// both backends and the plain-C++ unit tests can include it.

#include <draxul/renderer_state.h>

#include <algorithm>
#include <cstddef>

namespace draxul::grid_slot_upload
{

// True when a buffer of `capacity_bytes` holds the whole serialized state and
// every instance index the bg and fg draws will read.
[[nodiscard]] inline bool storage_covers_state(size_t capacity_bytes, const RendererState& state)
{
    const int instances = std::max(state.bg_instances(), state.fg_instances());
    if (instances < 0)
        return false;
    const size_t draw_bytes = static_cast<size_t>(instances) * sizeof(GpuCell);
    return capacity_bytes >= state.buffer_size_bytes() && capacity_bytes >= draw_bytes;
}

// Uploads `state` into one frame slot's storage. `Storage` provides:
//   size_t capacity() const;          // bytes currently backing the slot
//   bool grow(size_t required_bytes); // replace with >= required bytes; on
//                                     // failure the old storage must remain
//   std::byte* map();                 // writable pointer, or nullptr
// Returns true only when the slot now contains the complete current state and
// may be drawn with the state's instance counts.
template <typename Storage>
[[nodiscard]] bool upload(RendererState& state, Storage& storage)
{
    const size_t required = state.buffer_size_bytes();
    if (storage.capacity() < required && !storage.grow(required))
        return false;
    if (!storage_covers_state(storage.capacity(), state))
        return false;

    std::byte* mapped = storage.map();
    if (!mapped)
        return false;

    state.copy_to(mapped);
    state.clear_dirty();
    return true;
}

} // namespace draxul::grid_slot_upload
