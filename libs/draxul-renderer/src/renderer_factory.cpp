// Platform-specific renderer factory owned by draxul-renderer so app code only
// depends on the public renderer API.

#include <draxul/renderer.h>

#include <draxul/perf_timing.h>

#ifdef __APPLE__
#include "metal/metal_renderer.h"
#else
#include "vulkan/vk_renderer.h"
#endif

namespace draxul
{

RendererBundle create_renderer(int atlas_size, RendererOptions options)
{
    PERF_MEASURE();
#ifdef __APPLE__
    return RendererBundle{ std::make_unique<MetalRenderer>(atlas_size, options) };
#else
    return RendererBundle{ std::make_unique<VkRenderer>(atlas_size, options) };
#endif
}

} // namespace draxul
