#pragma once

#include <draxul/plugin_api.h>

#include <vk_mem_alloc.h>

#include <string>

// Lazy per-adapter VMA allocator creation from a borrowed Vulkan frame.
// Vulkan-only by placement: this header is consumed solely by the Vulkan
// half of dual-backend adapter TUs and the implementation builds inside
// draxul-vulkan-resources, which only exists on non-Apple platforms.

namespace draxul::plugin_support
{

// Creates `allocator` from the frame's instance/physical-device/device on
// first use; later calls are no-ops. On failure returns false and fills
// `error` with "<display_name> could not create its Vulkan allocator
// (<VkResult>)".
bool ensure_allocator(VmaAllocator& allocator,
    const DraxulPluginVulkanFrameV2& frame, const char* display_name,
    std::string& error);

// Destroys a lazily created allocator; safe when none was ever created.
void destroy_allocator(VmaAllocator& allocator);

} // namespace draxul::plugin_support
