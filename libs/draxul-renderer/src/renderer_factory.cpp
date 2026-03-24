// Platform-specific renderer factory owned by draxul-renderer so app code only
// depends on the public renderer API.

#include <draxul/renderer.h>

#ifdef __APPLE__
#include "metal/metal_renderer.h"
#else
#include "vulkan/vk_renderer.h"
#endif

namespace draxul
{

RendererBundle create_renderer(int atlas_size)
{
#ifdef __APPLE__
    return RendererBundle{ std::make_unique<MetalRenderer>(atlas_size) };
#else
    return RendererBundle{ std::make_unique<VkRenderer>(atlas_size) };
#endif
}

} // namespace draxul
