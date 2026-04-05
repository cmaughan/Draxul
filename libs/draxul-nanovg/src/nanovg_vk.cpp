// Custom Vulkan NanoVG backend — implements the NVGparams interface.
// Modeled on the Metal backend (nanovg_mtl.mm) and the nanovg_gl.h reference.

#include "nanovg_vk.h"
#include "nanovg.h"

#include <algorithm>
#include <cstring>
#include <draxul/log.h>
#include <draxul/runtime_path.h>
#include <fstream>
#include <unordered_map>
#include <vector>

namespace draxul
{

// ---------------------------------------------------------------------------
// Internal types matching the NanoVG GL reference backend
// ---------------------------------------------------------------------------

enum VkNVGshaderType
{
    VNVG_SHADER_FILLGRAD = 0,
    VNVG_SHADER_FILLIMG = 1,
    VNVG_SHADER_SIMPLE = 2,
    VNVG_SHADER_IMG = 3,
};

enum VkNVGcallType
{
    VNVG_NONE = 0,
    VNVG_FILL,
    VNVG_CONVEXFILL,
    VNVG_STROKE,
    VNVG_TRIANGLES,
};

struct VkNVGblend
{
    VkBlendFactor srcRGB;
    VkBlendFactor dstRGB;
    VkBlendFactor srcAlpha;
    VkBlendFactor dstAlpha;
};

struct VkNVGcall
{
    int type = VNVG_NONE;
    int image = 0;
    int pathOffset = 0;
    int pathCount = 0;
    int triangleOffset = 0;
    int triangleCount = 0;
    int uniformOffset = 0;
    VkNVGblend blendFunc{};
};

struct VkNVGpath
{
    int fillOffset = 0;
    int fillCount = 0;
    int strokeOffset = 0;
    int strokeCount = 0;
};

// Per-draw-call uniform block (matches shader FragUniforms).
// Must match the std140 layout in nanovg.frag.
// Padded to 256 bytes for dynamic uniform buffer offset alignment.
struct VkNVGfragUniforms
{
    // mat3 stored as 3 x vec4 (column-major, padded for std140)
    float scissorMat[12]; // 3 x vec4
    float paintMat[12]; // 3 x vec4
    float innerCol[4];
    float outerCol[4];
    float scissorExt[2];
    float scissorScale[2];
    float extent[2];
    float radius;
    float feather;
    float strokeMult;
    float strokeThr;
    int texType;
    int type;
    // Pad to 256 bytes
    uint8_t _pad[256 - (12 + 12 + 4 + 4 + 2 + 2 + 2 + 1 + 1 + 1 + 1 + 1 + 1) * 4];
};
static_assert(sizeof(VkNVGfragUniforms) == 256);

struct VkNVGtexture
{
    int texId = 0;
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkImageView imageView = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    int width = 0;
    int height = 0;
    int type = 0;
    int flags = 0;
};

// ---------------------------------------------------------------------------
// Vulkan NanoVG context — the userPtr for NVGparams
// ---------------------------------------------------------------------------
struct VkNVGcontext
{
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;
    VkFormat colorFormat = VK_FORMAT_UNDEFINED;

    // Pipeline objects
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipelineFillAA = VK_NULL_HANDLE;
    VkPipeline pipelineFillNoAA = VK_NULL_HANDLE;
    VkPipeline pipelineStencilOnly = VK_NULL_HANDLE;
    VkPipeline pipelineStencilFringe = VK_NULL_HANDLE;
    VkPipeline pipelineStencilCover = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;

    // Stencil image (owned)
    VkImage stencilImage = VK_NULL_HANDLE;
    VmaAllocation stencilAllocation = VK_NULL_HANDLE;
    VkImageView stencilView = VK_NULL_HANDLE;
    int stencilW = 0;
    int stencilH = 0;

    // Dummy 1x1 white texture
    VkNVGtexture dummyTex{};

    // Per-frame state
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkImage swapchainImage = VK_NULL_HANDLE;
    VkImageView swapchainImageView = VK_NULL_HANDLE;
    uint32_t frameIndex = 0;

    float viewWidth = 0;
    float viewHeight = 0;
    float devicePixelRatio = 1.0f;
    int flags = 0;

    // Deferred draw state
    std::vector<VkNVGcall> calls;
    std::vector<VkNVGpath> paths;
    std::vector<NVGvertex> verts;
    std::vector<uint8_t> uniforms;

    // Reused each frame to avoid per-frame heap allocation.
    std::unordered_map<int, VkDescriptorSet> textureDescriptorSets;

    // Texture management
    std::vector<VkNVGtexture> textures;
    int textureIdCounter = 0;

    // Shader modules (owned)
    VkShaderModule vertModule = VK_NULL_HANDLE;
    VkShaderModule fragModule = VK_NULL_HANDLE;

    // Per-frame resource cleanup (double-buffered)
    static constexpr int kMaxFramesInFlight = 3;
    struct FrameResources
    {
        std::vector<VkBuffer> buffers;
        std::vector<VmaAllocation> allocations;
        std::vector<VkFramebuffer> framebuffers;
        std::vector<VkDescriptorSet> descriptorSets;
    };
    FrameResources frameResources[kMaxFramesInFlight];
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static VkBlendFactor vknvg__blendFactor(int factor)
{
    switch (factor)
    {
    case NVG_ZERO:
        return VK_BLEND_FACTOR_ZERO;
    case NVG_ONE:
        return VK_BLEND_FACTOR_ONE;
    case NVG_SRC_COLOR:
        return VK_BLEND_FACTOR_SRC_COLOR;
    case NVG_ONE_MINUS_SRC_COLOR:
        return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    case NVG_DST_COLOR:
        return VK_BLEND_FACTOR_DST_COLOR;
    case NVG_ONE_MINUS_DST_COLOR:
        return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
    case NVG_SRC_ALPHA:
        return VK_BLEND_FACTOR_SRC_ALPHA;
    case NVG_ONE_MINUS_SRC_ALPHA:
        return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case NVG_DST_ALPHA:
        return VK_BLEND_FACTOR_DST_ALPHA;
    case NVG_ONE_MINUS_DST_ALPHA:
        return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    case NVG_SRC_ALPHA_SATURATE:
        return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
    default:
        return VK_BLEND_FACTOR_ONE;
    }
}

static VkNVGblend vknvg__blendCompositeOperation(NVGcompositeOperationState op)
{
    VkNVGblend blend;
    blend.srcRGB = vknvg__blendFactor(op.srcRGB);
    blend.dstRGB = vknvg__blendFactor(op.dstRGB);
    blend.srcAlpha = vknvg__blendFactor(op.srcAlpha);
    blend.dstAlpha = vknvg__blendFactor(op.dstAlpha);
    return blend;
}

static VkNVGtexture* vknvg__findTexture(VkNVGcontext* vk, int texId)
{
    for (auto& tex : vk->textures)
    {
        if (tex.texId == texId)
            return &tex;
    }
    return nullptr;
}

static void vknvg__xformToMat3x4(float* m3, const float* t)
{
    m3[0] = t[0];
    m3[1] = t[1];
    m3[2] = 0.0f;
    m3[3] = 0.0f;
    m3[4] = t[2];
    m3[5] = t[3];
    m3[6] = 0.0f;
    m3[7] = 0.0f;
    m3[8] = t[4];
    m3[9] = t[5];
    m3[10] = 1.0f;
    m3[11] = 0.0f;
}

static VkNVGfragUniforms* vknvg__fragUniformPtr(VkNVGcontext* vk, int offset)
{
    return reinterpret_cast<VkNVGfragUniforms*>(vk->uniforms.data() + offset);
}

static int vknvg__allocFragUniforms(VkNVGcontext* vk, int n)
{
    const int structSize = sizeof(VkNVGfragUniforms);
    int offset = static_cast<int>(vk->uniforms.size());
    vk->uniforms.resize(offset + n * structSize, 0);
    return offset;
}

static void vknvg__convertPaint(VkNVGcontext* vk, VkNVGfragUniforms* frag,
    NVGpaint* paint, NVGscissor* scissor, float width, float fringe, float strokeThr)
{
    memset(frag, 0, sizeof(*frag));

    frag->innerCol[0] = paint->innerColor.r * paint->innerColor.a;
    frag->innerCol[1] = paint->innerColor.g * paint->innerColor.a;
    frag->innerCol[2] = paint->innerColor.b * paint->innerColor.a;
    frag->innerCol[3] = paint->innerColor.a;
    frag->outerCol[0] = paint->outerColor.r * paint->outerColor.a;
    frag->outerCol[1] = paint->outerColor.g * paint->outerColor.a;
    frag->outerCol[2] = paint->outerColor.b * paint->outerColor.a;
    frag->outerCol[3] = paint->outerColor.a;

    if (scissor->extent[0] < -0.5f || scissor->extent[1] < -0.5f)
    {
        memset(frag->scissorMat, 0, sizeof(frag->scissorMat));
        frag->scissorExt[0] = 1.0f;
        frag->scissorExt[1] = 1.0f;
        frag->scissorScale[0] = 1.0f;
        frag->scissorScale[1] = 1.0f;
    }
    else
    {
        float invxform[6];
        nvgTransformInverse(invxform, scissor->xform);
        vknvg__xformToMat3x4(frag->scissorMat, invxform);
        frag->scissorExt[0] = scissor->extent[0];
        frag->scissorExt[1] = scissor->extent[1];
        frag->scissorScale[0] = sqrtf(scissor->xform[0] * scissor->xform[0] + scissor->xform[2] * scissor->xform[2]) / fringe;
        frag->scissorScale[1] = sqrtf(scissor->xform[1] * scissor->xform[1] + scissor->xform[3] * scissor->xform[3]) / fringe;
    }

    frag->extent[0] = paint->extent[0];
    frag->extent[1] = paint->extent[1];
    frag->strokeMult = (width * 0.5f + fringe * 0.5f) / fringe;
    frag->strokeThr = strokeThr;

    if (paint->image != 0)
    {
        VkNVGtexture* tex = vknvg__findTexture(vk, paint->image);
        if (tex == nullptr)
            return;
        if ((tex->flags & NVG_IMAGE_FLIPY) != 0)
        {
            float m1[6], m2[6];
            nvgTransformTranslate(m1, 0.0f, frag->extent[1] * 0.5f);
            nvgTransformMultiply(m1, paint->xform);
            nvgTransformScale(m2, 1.0f, -1.0f);
            nvgTransformMultiply(m2, m1);
            nvgTransformTranslate(m1, 0.0f, -frag->extent[1] * 0.5f);
            nvgTransformMultiply(m1, m2);
            float invxform[6];
            nvgTransformInverse(invxform, m1);
            vknvg__xformToMat3x4(frag->paintMat, invxform);
        }
        else
        {
            float invxform[6];
            nvgTransformInverse(invxform, paint->xform);
            vknvg__xformToMat3x4(frag->paintMat, invxform);
        }
        frag->type = VNVG_SHADER_FILLIMG;
        if (tex->type == NVG_TEXTURE_RGBA)
            frag->texType = (tex->flags & NVG_IMAGE_PREMULTIPLIED) ? 0 : 1;
        else
            frag->texType = 2;
    }
    else
    {
        frag->type = VNVG_SHADER_FILLGRAD;
        frag->radius = paint->radius;
        frag->feather = paint->feather;
        float invxform[6];
        nvgTransformInverse(invxform, paint->xform);
        vknvg__xformToMat3x4(frag->paintMat, invxform);
    }
}

// ---------------------------------------------------------------------------
// Stencil image management
// ---------------------------------------------------------------------------

static VkFormat vknvg__findStencilFormat(VkPhysicalDevice physicalDevice)
{
    // Prefer pure stencil, fall back to depth+stencil
    VkFormat candidates[] = { VK_FORMAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT_S8_UINT };
    for (auto fmt : candidates)
    {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(physicalDevice, fmt, &props);
        if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
            return fmt;
    }
    return VK_FORMAT_D24_UNORM_S8_UINT; // fallback
}

static bool vknvg__hasDepthComponent(VkFormat format)
{
    return format == VK_FORMAT_D24_UNORM_S8_UINT || format == VK_FORMAT_D32_SFLOAT_S8_UINT
        || format == VK_FORMAT_D16_UNORM_S8_UINT;
}

static void vknvg__destroyStencil(VkNVGcontext* vk)
{
    if (vk->stencilView != VK_NULL_HANDLE)
        vkDestroyImageView(vk->device, vk->stencilView, nullptr);
    if (vk->stencilImage != VK_NULL_HANDLE)
        vmaDestroyImage(vk->allocator, vk->stencilImage, vk->stencilAllocation);
    vk->stencilView = VK_NULL_HANDLE;
    vk->stencilImage = VK_NULL_HANDLE;
    vk->stencilAllocation = VK_NULL_HANDLE;
    vk->stencilW = 0;
    vk->stencilH = 0;
}

static void vknvg__ensureStencilImage(VkNVGcontext* vk, int w, int h)
{
    if (vk->stencilImage != VK_NULL_HANDLE && vk->stencilW == w && vk->stencilH == h)
        return;

    vknvg__destroyStencil(vk);

    VkFormat stencilFormat = vknvg__findStencilFormat(vk->physicalDevice);

    VkImageCreateInfo imgCI = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    imgCI.imageType = VK_IMAGE_TYPE_2D;
    imgCI.format = stencilFormat;
    imgCI.extent = { static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1 };
    imgCI.mipLevels = 1;
    imgCI.arrayLayers = 1;
    imgCI.samples = VK_SAMPLE_COUNT_1_BIT;
    imgCI.tiling = VK_IMAGE_TILING_OPTIMAL;
    imgCI.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

    VmaAllocationCreateInfo allocCI = {};
    allocCI.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateImage(vk->allocator, &imgCI, &allocCI, &vk->stencilImage, &vk->stencilAllocation, nullptr) != VK_SUCCESS)
        return;

    VkImageViewCreateInfo viewCI = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    viewCI.image = vk->stencilImage;
    viewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewCI.format = stencilFormat;
    viewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
    if (vknvg__hasDepthComponent(stencilFormat))
        viewCI.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_DEPTH_BIT;
    viewCI.subresourceRange.levelCount = 1;
    viewCI.subresourceRange.layerCount = 1;

    if (vkCreateImageView(vk->device, &viewCI, nullptr, &vk->stencilView) != VK_SUCCESS)
    {
        vmaDestroyImage(vk->allocator, vk->stencilImage, vk->stencilAllocation);
        vk->stencilImage = VK_NULL_HANDLE;
        vk->stencilAllocation = VK_NULL_HANDLE;
        return;
    }

    vk->stencilW = w;
    vk->stencilH = h;
}

// ---------------------------------------------------------------------------
// Shader loading helper
// ---------------------------------------------------------------------------

static std::vector<char> vknvg__loadShaderBytes(const char* name)
{
    const auto shader_dir = bundled_asset_path("shaders");
    const auto shader_path = shader_dir / name;

    std::ifstream file(shader_path, std::ios::ate | std::ios::binary);
    if (!file.is_open())
    {
        DRAXUL_LOG_ERROR(LogCategory::Renderer,
            "NanoVG Vulkan failed to open shader: %s",
            shader_path.string().c_str());
        return {};
    }

    const size_t size = static_cast<size_t>(file.tellg());
    std::vector<char> data(size);
    file.seekg(0);
    file.read(data.data(), static_cast<std::streamsize>(size));
    return data;
}

static VkShaderModule vknvg__loadShader(VkDevice device, const char* name)
{
    auto data = vknvg__loadShaderBytes(name);
    if (data.empty())
        return VK_NULL_HANDLE;

    VkShaderModuleCreateInfo ci = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    ci.codeSize = data.size();
    ci.pCode = reinterpret_cast<const uint32_t*>(data.data());

    VkShaderModule mod = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &ci, nullptr, &mod) != VK_SUCCESS)
    {
        DRAXUL_LOG_ERROR(LogCategory::Renderer, "NanoVG Vulkan failed to create shader module: %s", name);
        return VK_NULL_HANDLE;
    }
    return mod;
}

// ---------------------------------------------------------------------------
// Texture helpers
// ---------------------------------------------------------------------------

static void vknvg__destroyTexture(VkNVGcontext* vk, VkNVGtexture& tex)
{
    if (tex.descriptorSet != VK_NULL_HANDLE)
        vkFreeDescriptorSets(vk->device, vk->descriptorPool, 1, &tex.descriptorSet);
    if (tex.imageView != VK_NULL_HANDLE)
        vkDestroyImageView(vk->device, tex.imageView, nullptr);
    if (tex.image != VK_NULL_HANDLE)
        vmaDestroyImage(vk->allocator, tex.image, tex.allocation);
    tex = {};
}

static VkDescriptorSet vknvg__allocDescriptorSet(VkNVGcontext* vk)
{
    VkDescriptorSetAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    allocInfo.descriptorPool = vk->descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &vk->descriptorSetLayout;

    VkDescriptorSet ds = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(vk->device, &allocInfo, &ds) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    return ds;
}

// ---------------------------------------------------------------------------
// NVGparams callbacks
// ---------------------------------------------------------------------------

static int vknvg__renderCreate(void* uptr)
{
    VkNVGcontext* vk = static_cast<VkNVGcontext*>(uptr);

    // Load SPIR-V shaders
    vk->vertModule = vknvg__loadShader(vk->device, "nanovg.vert.spv");
    vk->fragModule = vknvg__loadShader(vk->device, "nanovg.frag.spv");
    if (vk->vertModule == VK_NULL_HANDLE || vk->fragModule == VK_NULL_HANDLE)
    {
        DRAXUL_LOG_ERROR(LogCategory::Renderer, "NanoVG Vulkan failed to load shaders");
        return 0;
    }

    // Find stencil format
    VkFormat stencilFormat = vknvg__findStencilFormat(vk->physicalDevice);

    // Create render pass (color load + stencil clear)
    {
        VkAttachmentDescription attachments[2] = {};

        // Color attachment — load existing content
        attachments[0].format = vk->colorFormat;
        attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[0].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        // Stencil attachment — clear to 0
        attachments[1].format = stencilFormat;
        attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference colorRef = {};
        colorRef.attachment = 0;
        colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentReference stencilRef = {};
        stencilRef.attachment = 1;
        stencilRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass = {};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorRef;
        subpass.pDepthStencilAttachment = &stencilRef;

        VkSubpassDependency deps[2] = {};
        deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        deps[0].dstSubpass = 0;
        deps[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        deps[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        deps[1].srcSubpass = 0;
        deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        deps[1].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        deps[1].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo rpCI = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
        rpCI.attachmentCount = 2;
        rpCI.pAttachments = attachments;
        rpCI.subpassCount = 1;
        rpCI.pSubpasses = &subpass;
        rpCI.dependencyCount = 2;
        rpCI.pDependencies = deps;

        if (vkCreateRenderPass(vk->device, &rpCI, nullptr, &vk->renderPass) != VK_SUCCESS)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "NanoVG Vulkan failed to create render pass");
            return 0;
        }
    }

    // Descriptor set layout: binding 0 = uniform buffer (dynamic), binding 1 = combined image sampler
    {
        VkDescriptorSetLayoutBinding bindings[2] = {};
        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo ci = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        ci.bindingCount = 2;
        ci.pBindings = bindings;

        if (vkCreateDescriptorSetLayout(vk->device, &ci, nullptr, &vk->descriptorSetLayout) != VK_SUCCESS)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "NanoVG Vulkan failed to create descriptor set layout");
            return 0;
        }
    }

    // Descriptor pool
    {
        VkDescriptorPoolSize poolSizes[2] = {};
        poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        poolSizes[0].descriptorCount = 1024;
        poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSizes[1].descriptorCount = 1024;

        VkDescriptorPoolCreateInfo ci = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        ci.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        ci.maxSets = 1024;
        ci.poolSizeCount = 2;
        ci.pPoolSizes = poolSizes;

        if (vkCreateDescriptorPool(vk->device, &ci, nullptr, &vk->descriptorPool) != VK_SUCCESS)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "NanoVG Vulkan failed to create descriptor pool");
            return 0;
        }
    }

    // Pipeline layout: push constants for viewSize, one descriptor set
    {
        VkPushConstantRange pushRange = {};
        pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        pushRange.offset = 0;
        pushRange.size = sizeof(float) * 2;

        VkPipelineLayoutCreateInfo ci = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        ci.setLayoutCount = 1;
        ci.pSetLayouts = &vk->descriptorSetLayout;
        ci.pushConstantRangeCount = 1;
        ci.pPushConstantRanges = &pushRange;

        if (vkCreatePipelineLayout(vk->device, &ci, nullptr, &vk->pipelineLayout) != VK_SUCCESS)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "NanoVG Vulkan failed to create pipeline layout");
            return 0;
        }
    }

    // Sampler
    {
        VkSamplerCreateInfo ci = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
        ci.magFilter = VK_FILTER_LINEAR;
        ci.minFilter = VK_FILTER_LINEAR;
        ci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        ci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        ci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        ci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

        if (vkCreateSampler(vk->device, &ci, nullptr, &vk->sampler) != VK_SUCCESS)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "NanoVG Vulkan failed to create sampler");
            return 0;
        }
    }

    // Shared pipeline state
    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vk->vertModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = vk->fragModule;
    stages[1].pName = "main";

    // Vertex input: pos (float2) + tcoord (float2) = NVGvertex (16 bytes)
    VkVertexInputBindingDescription vertBinding = {};
    vertBinding.binding = 0;
    vertBinding.stride = sizeof(NVGvertex);
    vertBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription vertAttrs[2] = {};
    vertAttrs[0].location = 0;
    vertAttrs[0].binding = 0;
    vertAttrs[0].format = VK_FORMAT_R32G32_SFLOAT;
    vertAttrs[0].offset = 0;
    vertAttrs[1].location = 1;
    vertAttrs[1].binding = 0;
    vertAttrs[1].format = VK_FORMAT_R32G32_SFLOAT;
    vertAttrs[1].offset = sizeof(float) * 2;

    VkPipelineVertexInputStateCreateInfo vertexInput = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &vertBinding;
    vertexInput.vertexAttributeDescriptionCount = 2;
    vertexInput.pVertexAttributeDescriptions = vertAttrs;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    // NanoVG emits 2D UI geometry with a mix of triangle lists and strips. The
    // Vulkan backend does not need face culling here, and disabling it avoids
    // platform-specific winding differences making UI chrome disappear.
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisampling = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkDynamicState dynamicStates[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
        VK_DYNAMIC_STATE_STENCIL_REFERENCE,
        VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,
        VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,
    };
    VkPipelineDynamicStateCreateInfo dynamicState = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    dynamicState.dynamicStateCount = 5;
    dynamicState.pDynamicStates = dynamicStates;

    // Base depth/stencil state used for normal non-stencil draws.
    VkPipelineDepthStencilStateCreateInfo depthStencil = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;
    depthStencil.front.compareOp = VK_COMPARE_OP_ALWAYS;
    depthStencil.front.failOp = VK_STENCIL_OP_KEEP;
    depthStencil.front.depthFailOp = VK_STENCIL_OP_KEEP;
    depthStencil.front.passOp = VK_STENCIL_OP_KEEP;
    depthStencil.front.compareMask = 0xFF;
    depthStencil.front.writeMask = 0xFF;
    depthStencil.back = depthStencil.front;

    // Non-convex fills rely on the same stencil phases as the GL/Metal backends.
    VkPipelineDepthStencilStateCreateInfo depthStencilFill = depthStencil;
    depthStencilFill.stencilTestEnable = VK_TRUE;
    depthStencilFill.front.passOp = VK_STENCIL_OP_INCREMENT_AND_WRAP;
    depthStencilFill.back.passOp = VK_STENCIL_OP_DECREMENT_AND_WRAP;

    VkPipelineDepthStencilStateCreateInfo depthStencilFringe = depthStencil;
    depthStencilFringe.stencilTestEnable = VK_TRUE;
    depthStencilFringe.front.compareOp = VK_COMPARE_OP_EQUAL;
    depthStencilFringe.back.compareOp = VK_COMPARE_OP_EQUAL;

    VkPipelineDepthStencilStateCreateInfo depthStencilCover = depthStencil;
    depthStencilCover.stencilTestEnable = VK_TRUE;
    depthStencilCover.front.compareOp = VK_COMPARE_OP_NOT_EQUAL;
    depthStencilCover.front.failOp = VK_STENCIL_OP_ZERO;
    depthStencilCover.front.depthFailOp = VK_STENCIL_OP_ZERO;
    depthStencilCover.front.passOp = VK_STENCIL_OP_ZERO;
    depthStencilCover.back = depthStencilCover.front;

    // Create fill AA pipeline
    {
        VkPipelineColorBlendAttachmentState colorBlend = {};
        colorBlend.blendEnable = VK_TRUE;
        colorBlend.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlend.colorBlendOp = VK_BLEND_OP_ADD;
        colorBlend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlend.alphaBlendOp = VK_BLEND_OP_ADD;
        colorBlend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
            | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

        VkPipelineColorBlendStateCreateInfo blendState = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
        blendState.attachmentCount = 1;
        blendState.pAttachments = &colorBlend;

        VkGraphicsPipelineCreateInfo pipeCI = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
        pipeCI.stageCount = 2;
        pipeCI.pStages = stages;
        pipeCI.pVertexInputState = &vertexInput;
        pipeCI.pInputAssemblyState = &inputAssembly;
        pipeCI.pViewportState = &viewportState;
        pipeCI.pRasterizationState = &rasterizer;
        pipeCI.pMultisampleState = &multisampling;
        pipeCI.pDepthStencilState = &depthStencil;
        pipeCI.pColorBlendState = &blendState;
        pipeCI.pDynamicState = &dynamicState;
        pipeCI.layout = vk->pipelineLayout;
        pipeCI.renderPass = vk->renderPass;
        pipeCI.subpass = 0;

        if (vkCreateGraphicsPipelines(vk->device, VK_NULL_HANDLE, 1, &pipeCI, nullptr, &vk->pipelineFillAA) != VK_SUCCESS)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "NanoVG Vulkan failed to create fill pipeline");
            return 0;
        }
    }

    // Create stencil-only pipeline (no color writes)
    {
        VkPipelineColorBlendAttachmentState colorBlend = {};
        colorBlend.blendEnable = VK_FALSE;
        colorBlend.colorWriteMask = 0; // no color writes

        VkPipelineColorBlendStateCreateInfo blendState = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
        blendState.attachmentCount = 1;
        blendState.pAttachments = &colorBlend;

        // Stencil-only needs no cull (both faces contribute)
        VkPipelineRasterizationStateCreateInfo stencilRaster = rasterizer;
        stencilRaster.cullMode = VK_CULL_MODE_NONE;

        VkGraphicsPipelineCreateInfo pipeCI = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
        pipeCI.stageCount = 2;
        pipeCI.pStages = stages;
        pipeCI.pVertexInputState = &vertexInput;
        pipeCI.pInputAssemblyState = &inputAssembly;
        pipeCI.pViewportState = &viewportState;
        pipeCI.pRasterizationState = &stencilRaster;
        pipeCI.pMultisampleState = &multisampling;
        pipeCI.pDepthStencilState = &depthStencilFill;
        pipeCI.pColorBlendState = &blendState;
        pipeCI.pDynamicState = &dynamicState;
        pipeCI.layout = vk->pipelineLayout;
        pipeCI.renderPass = vk->renderPass;
        pipeCI.subpass = 0;

        if (vkCreateGraphicsPipelines(vk->device, VK_NULL_HANDLE, 1, &pipeCI, nullptr, &vk->pipelineStencilOnly) != VK_SUCCESS)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "NanoVG Vulkan failed to create stencil-only pipeline");
            return 0;
        }
    }

    // Triangle strips are used for strokes and the non-convex cover quad.
    {
        VkPipelineColorBlendAttachmentState colorBlend = {};
        colorBlend.blendEnable = VK_TRUE;
        colorBlend.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlend.colorBlendOp = VK_BLEND_OP_ADD;
        colorBlend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlend.alphaBlendOp = VK_BLEND_OP_ADD;
        colorBlend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
            | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

        VkPipelineColorBlendStateCreateInfo blendState = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
        blendState.attachmentCount = 1;
        blendState.pAttachments = &colorBlend;

        VkPipelineInputAssemblyStateCreateInfo stripAssembly = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
        stripAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;

        VkGraphicsPipelineCreateInfo pipeCI = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
        pipeCI.stageCount = 2;
        pipeCI.pStages = stages;
        pipeCI.pVertexInputState = &vertexInput;
        pipeCI.pInputAssemblyState = &stripAssembly;
        pipeCI.pViewportState = &viewportState;
        pipeCI.pRasterizationState = &rasterizer;
        pipeCI.pMultisampleState = &multisampling;
        pipeCI.pDepthStencilState = &depthStencil;
        pipeCI.pColorBlendState = &blendState;
        pipeCI.pDynamicState = &dynamicState;
        pipeCI.layout = vk->pipelineLayout;
        pipeCI.renderPass = vk->renderPass;
        pipeCI.subpass = 0;

        if (vkCreateGraphicsPipelines(vk->device, VK_NULL_HANDLE, 1, &pipeCI, nullptr, &vk->pipelineFillNoAA) != VK_SUCCESS)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "NanoVG Vulkan failed to create strip pipeline");
            return 0;
        }
    }

    // Fringes for non-convex fills only draw where the fill stencil is still zero.
    {
        VkPipelineColorBlendAttachmentState colorBlend = {};
        colorBlend.blendEnable = VK_TRUE;
        colorBlend.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlend.colorBlendOp = VK_BLEND_OP_ADD;
        colorBlend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlend.alphaBlendOp = VK_BLEND_OP_ADD;
        colorBlend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
            | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

        VkPipelineColorBlendStateCreateInfo blendState = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
        blendState.attachmentCount = 1;
        blendState.pAttachments = &colorBlend;

        VkPipelineInputAssemblyStateCreateInfo stripAssembly = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
        stripAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;

        VkGraphicsPipelineCreateInfo pipeCI = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
        pipeCI.stageCount = 2;
        pipeCI.pStages = stages;
        pipeCI.pVertexInputState = &vertexInput;
        pipeCI.pInputAssemblyState = &stripAssembly;
        pipeCI.pViewportState = &viewportState;
        pipeCI.pRasterizationState = &rasterizer;
        pipeCI.pMultisampleState = &multisampling;
        pipeCI.pDepthStencilState = &depthStencilFringe;
        pipeCI.pColorBlendState = &blendState;
        pipeCI.pDynamicState = &dynamicState;
        pipeCI.layout = vk->pipelineLayout;
        pipeCI.renderPass = vk->renderPass;
        pipeCI.subpass = 0;

        if (vkCreateGraphicsPipelines(vk->device, VK_NULL_HANDLE, 1, &pipeCI, nullptr, &vk->pipelineStencilFringe) != VK_SUCCESS)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "NanoVG Vulkan failed to create stencil fringe pipeline");
            return 0;
        }
    }

    // Cover pass draws where stencil != 0 and zeroes it so later calls start clean.
    {
        VkPipelineColorBlendAttachmentState colorBlend = {};
        colorBlend.blendEnable = VK_TRUE;
        colorBlend.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlend.colorBlendOp = VK_BLEND_OP_ADD;
        colorBlend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlend.alphaBlendOp = VK_BLEND_OP_ADD;
        colorBlend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
            | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

        VkPipelineColorBlendStateCreateInfo blendState = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
        blendState.attachmentCount = 1;
        blendState.pAttachments = &colorBlend;

        VkPipelineInputAssemblyStateCreateInfo stripAssembly = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
        stripAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;

        VkGraphicsPipelineCreateInfo pipeCI = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
        pipeCI.stageCount = 2;
        pipeCI.pStages = stages;
        pipeCI.pVertexInputState = &vertexInput;
        pipeCI.pInputAssemblyState = &stripAssembly;
        pipeCI.pViewportState = &viewportState;
        pipeCI.pRasterizationState = &rasterizer;
        pipeCI.pMultisampleState = &multisampling;
        pipeCI.pDepthStencilState = &depthStencilCover;
        pipeCI.pColorBlendState = &blendState;
        pipeCI.pDynamicState = &dynamicState;
        pipeCI.layout = vk->pipelineLayout;
        pipeCI.renderPass = vk->renderPass;
        pipeCI.subpass = 0;

        if (vkCreateGraphicsPipelines(vk->device, VK_NULL_HANDLE, 1, &pipeCI, nullptr, &vk->pipelineStencilCover) != VK_SUCCESS)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "NanoVG Vulkan failed to create stencil cover pipeline");
            return 0;
        }
    }

    // Create dummy 1x1 white texture
    {
        VkImageCreateInfo imgCI = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        imgCI.imageType = VK_IMAGE_TYPE_2D;
        imgCI.format = VK_FORMAT_R8G8B8A8_UNORM;
        imgCI.extent = { 1, 1, 1 };
        imgCI.mipLevels = 1;
        imgCI.arrayLayers = 1;
        imgCI.samples = VK_SAMPLE_COUNT_1_BIT;
        imgCI.tiling = VK_IMAGE_TILING_LINEAR;
        imgCI.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

        VmaAllocationCreateInfo allocCI = {};
        allocCI.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

        if (vmaCreateImage(vk->allocator, &imgCI, &allocCI, &vk->dummyTex.image, &vk->dummyTex.allocation, nullptr) != VK_SUCCESS)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "NanoVG Vulkan failed to create dummy texture image");
            return 0;
        }

        // Write white pixel
        void* mapped = nullptr;
        vmaMapMemory(vk->allocator, vk->dummyTex.allocation, &mapped);
        uint32_t white = 0xFFFFFFFF;
        memcpy(mapped, &white, 4);
        vmaUnmapMemory(vk->allocator, vk->dummyTex.allocation);

        VkImageViewCreateInfo viewCI = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        viewCI.image = vk->dummyTex.image;
        viewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewCI.format = VK_FORMAT_R8G8B8A8_UNORM;
        viewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewCI.subresourceRange.levelCount = 1;
        viewCI.subresourceRange.layerCount = 1;

        if (vkCreateImageView(vk->device, &viewCI, nullptr, &vk->dummyTex.imageView) != VK_SUCCESS)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "NanoVG Vulkan failed to create dummy texture image view");
            return 0;
        }

        vk->dummyTex.width = 1;
        vk->dummyTex.height = 1;
        vk->dummyTex.texId = -1; // internal
    }

    return 1;
}

static int vknvg__renderCreateTexture(void* uptr, int type, int w, int h, int imageFlags, const unsigned char* data)
{
    VkNVGcontext* vk = static_cast<VkNVGcontext*>(uptr);

    VkFormat format = (type == NVG_TEXTURE_RGBA) ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R8_UNORM;
    int bpp = (type == NVG_TEXTURE_RGBA) ? 4 : 1;

    VkImageCreateInfo imgCI = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    imgCI.imageType = VK_IMAGE_TYPE_2D;
    imgCI.format = format;
    imgCI.extent = { static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1 };
    imgCI.mipLevels = 1;
    imgCI.arrayLayers = 1;
    imgCI.samples = VK_SAMPLE_COUNT_1_BIT;
    imgCI.tiling = VK_IMAGE_TILING_LINEAR;
    imgCI.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    VmaAllocationCreateInfo allocCI = {};
    allocCI.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

    VkNVGtexture tex;
    if (vmaCreateImage(vk->allocator, &imgCI, &allocCI, &tex.image, &tex.allocation, nullptr) != VK_SUCCESS)
    {
        DRAXUL_LOG_ERROR(LogCategory::Renderer,
            "NanoVG Vulkan failed to create texture image (%dx%d type=%d flags=0x%x)",
            w, h, type, imageFlags);
        return 0;
    }

    if (data)
    {
        void* mapped = nullptr;
        vmaMapMemory(vk->allocator, tex.allocation, &mapped);
        memcpy(mapped, data, w * h * bpp);
        vmaUnmapMemory(vk->allocator, tex.allocation);
    }

    VkImageViewCreateInfo viewCI = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    viewCI.image = tex.image;
    viewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewCI.format = format;
    viewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewCI.subresourceRange.levelCount = 1;
    viewCI.subresourceRange.layerCount = 1;

    if (vkCreateImageView(vk->device, &viewCI, nullptr, &tex.imageView) != VK_SUCCESS)
    {
        DRAXUL_LOG_ERROR(LogCategory::Renderer,
            "NanoVG Vulkan failed to create texture image view (%dx%d type=%d flags=0x%x)",
            w, h, type, imageFlags);
        vmaDestroyImage(vk->allocator, tex.image, tex.allocation);
        return 0;
    }

    tex.texId = ++vk->textureIdCounter;
    tex.width = w;
    tex.height = h;
    tex.type = type;
    tex.flags = imageFlags;
    vk->textures.push_back(tex);
    return tex.texId;
}

static int vknvg__renderDeleteTexture(void* uptr, int image)
{
    VkNVGcontext* vk = static_cast<VkNVGcontext*>(uptr);
    for (auto it = vk->textures.begin(); it != vk->textures.end(); ++it)
    {
        if (it->texId == image)
        {
            vknvg__destroyTexture(vk, *it);
            vk->textures.erase(it);
            return 1;
        }
    }
    return 0;
}

static int vknvg__renderUpdateTexture(void* uptr, int image, int x, int y, int w, int h, const unsigned char* data)
{
    VkNVGcontext* vk = static_cast<VkNVGcontext*>(uptr);
    VkNVGtexture* tex = vknvg__findTexture(vk, image);
    if (!tex)
        return 0;

    int bpp = (tex->type == NVG_TEXTURE_RGBA) ? 4 : 1;

    void* mapped = nullptr;
    vmaMapMemory(vk->allocator, tex->allocation, &mapped);
    auto* dst = static_cast<uint8_t*>(mapped);
    const uint8_t* src = data + (y * tex->width + x) * bpp;
    for (int row = 0; row < h; row++)
    {
        memcpy(dst + ((y + row) * tex->width + x) * bpp, src + row * tex->width * bpp, w * bpp);
    }
    vmaUnmapMemory(vk->allocator, tex->allocation);
    return 1;
}

static int vknvg__renderGetTextureSize(void* uptr, int image, int* w, int* h)
{
    VkNVGcontext* vk = static_cast<VkNVGcontext*>(uptr);
    VkNVGtexture* tex = vknvg__findTexture(vk, image);
    if (!tex)
        return 0;
    if (w)
        *w = tex->width;
    if (h)
        *h = tex->height;
    return 1;
}

static void vknvg__renderViewport(void* uptr, float width, float height, float devicePixelRatio)
{
    VkNVGcontext* vk = static_cast<VkNVGcontext*>(uptr);
    vk->viewWidth = width;
    vk->viewHeight = height;
    vk->devicePixelRatio = devicePixelRatio;
}

static void vknvg__renderCancel(void* uptr)
{
    VkNVGcontext* vk = static_cast<VkNVGcontext*>(uptr);
    vk->calls.clear();
    vk->paths.clear();
    vk->verts.clear();
    vk->uniforms.clear();
}

// Convert triangle fan to triangle list
static int vknvg__fanToTriangles(VkNVGcontext* vk, const NVGvertex* fan, int nfan)
{
    if (nfan < 3)
        return 0;
    int offset = static_cast<int>(vk->verts.size());
    int ntris = nfan - 2;
    vk->verts.resize(offset + ntris * 3);
    for (int i = 0; i < ntris; i++)
    {
        vk->verts[offset + i * 3 + 0] = fan[0];
        vk->verts[offset + i * 3 + 1] = fan[i + 1];
        vk->verts[offset + i * 3 + 2] = fan[i + 2];
    }
    return ntris * 3;
}

static void vknvg__renderFill(void* uptr, NVGpaint* paint, NVGcompositeOperationState compositeOperation,
    NVGscissor* scissor, float fringe, const float* bounds,
    const NVGpath* paths, int npaths)
{
    VkNVGcontext* vk = static_cast<VkNVGcontext*>(uptr);

    VkNVGcall call;
    call.type = VNVG_FILL;
    call.pathOffset = static_cast<int>(vk->paths.size());
    call.pathCount = npaths;
    call.image = paint->image;
    call.blendFunc = vknvg__blendCompositeOperation(compositeOperation);

    if (npaths == 1 && paths[0].convex)
        call.type = VNVG_CONVEXFILL;

    for (int i = 0; i < npaths; i++)
    {
        VkNVGpath p;
        const NVGpath& src = paths[i];

        if (src.nfill > 0)
        {
            p.fillOffset = static_cast<int>(vk->verts.size());
            p.fillCount = vknvg__fanToTriangles(vk, src.fill, src.nfill);
        }

        if (src.nstroke > 0)
        {
            p.strokeOffset = static_cast<int>(vk->verts.size());
            p.strokeCount = src.nstroke;
            vk->verts.insert(vk->verts.end(), src.stroke, src.stroke + src.nstroke);
        }

        vk->paths.push_back(p);
    }

    if (call.type == VNVG_FILL)
    {
        call.triangleOffset = static_cast<int>(vk->verts.size());
        NVGvertex quad[4];
        quad[0] = { bounds[2], bounds[3], 0.5f, 1.0f };
        quad[1] = { bounds[2], bounds[1], 0.5f, 1.0f };
        quad[2] = { bounds[0], bounds[3], 0.5f, 1.0f };
        quad[3] = { bounds[0], bounds[1], 0.5f, 1.0f };
        vk->verts.insert(vk->verts.end(), quad, quad + 4);
        call.triangleCount = 4;

        call.uniformOffset = vknvg__allocFragUniforms(vk, 2);
        auto* frag = vknvg__fragUniformPtr(vk, call.uniformOffset);
        memset(frag, 0, sizeof(*frag));
        frag->strokeThr = -1.0f;
        frag->type = VNVG_SHADER_SIMPLE;

        vknvg__convertPaint(vk, vknvg__fragUniformPtr(vk, call.uniformOffset + sizeof(VkNVGfragUniforms)),
            paint, scissor, fringe, fringe, -1.0f);
    }
    else
    {
        call.uniformOffset = vknvg__allocFragUniforms(vk, 1);
        vknvg__convertPaint(vk, vknvg__fragUniformPtr(vk, call.uniformOffset),
            paint, scissor, fringe, fringe, -1.0f);
    }

    vk->calls.push_back(call);
}

static void vknvg__renderStroke(void* uptr, NVGpaint* paint, NVGcompositeOperationState compositeOperation,
    NVGscissor* scissor, float fringe, float strokeWidth,
    const NVGpath* paths, int npaths)
{
    VkNVGcontext* vk = static_cast<VkNVGcontext*>(uptr);

    VkNVGcall call;
    call.type = VNVG_STROKE;
    call.pathOffset = static_cast<int>(vk->paths.size());
    call.pathCount = npaths;
    call.image = paint->image;
    call.blendFunc = vknvg__blendCompositeOperation(compositeOperation);

    for (int i = 0; i < npaths; i++)
    {
        VkNVGpath p;
        const NVGpath& src = paths[i];
        if (src.nstroke > 0)
        {
            p.strokeOffset = static_cast<int>(vk->verts.size());
            p.strokeCount = src.nstroke;
            vk->verts.insert(vk->verts.end(), src.stroke, src.stroke + src.nstroke);
        }
        vk->paths.push_back(p);
    }

    if (vk->flags & NVG_STENCIL_STROKES)
    {
        call.uniformOffset = vknvg__allocFragUniforms(vk, 2);
        vknvg__convertPaint(vk, vknvg__fragUniformPtr(vk, call.uniformOffset),
            paint, scissor, strokeWidth, fringe, -1.0f);
        vknvg__convertPaint(vk, vknvg__fragUniformPtr(vk, call.uniformOffset + sizeof(VkNVGfragUniforms)),
            paint, scissor, strokeWidth, fringe, 1.0f - 0.5f / 255.0f);
    }
    else
    {
        call.uniformOffset = vknvg__allocFragUniforms(vk, 1);
        vknvg__convertPaint(vk, vknvg__fragUniformPtr(vk, call.uniformOffset),
            paint, scissor, strokeWidth, fringe, -1.0f);
    }

    vk->calls.push_back(call);
}

static void vknvg__renderTriangles(void* uptr, NVGpaint* paint, NVGcompositeOperationState compositeOperation,
    NVGscissor* scissor, const NVGvertex* verts, int nverts, float fringe)
{
    VkNVGcontext* vk = static_cast<VkNVGcontext*>(uptr);

    VkNVGcall call;
    call.type = VNVG_TRIANGLES;
    call.image = paint->image;
    call.blendFunc = vknvg__blendCompositeOperation(compositeOperation);
    call.triangleOffset = static_cast<int>(vk->verts.size());
    call.triangleCount = nverts;
    vk->verts.insert(vk->verts.end(), verts, verts + nverts);

    call.uniformOffset = vknvg__allocFragUniforms(vk, 1);
    auto* frag = vknvg__fragUniformPtr(vk, call.uniformOffset);
    vknvg__convertPaint(vk, frag, paint, scissor, 1.0f, fringe, -1.0f);
    frag->type = VNVG_SHADER_IMG;

    vk->calls.push_back(call);
}

// ---------------------------------------------------------------------------
// renderFlush — the main GPU work
// ---------------------------------------------------------------------------

static void vknvg__cleanupFrameResources(VkNVGcontext* vk, int slot)
{
    auto& fr = vk->frameResources[slot];
    if (!fr.descriptorSets.empty())
        vkFreeDescriptorSets(vk->device, vk->descriptorPool,
            static_cast<uint32_t>(fr.descriptorSets.size()), fr.descriptorSets.data());
    for (size_t i = 0; i < fr.buffers.size(); i++)
        vmaDestroyBuffer(vk->allocator, fr.buffers[i], fr.allocations[i]);
    for (auto fb : fr.framebuffers)
        vkDestroyFramebuffer(vk->device, fb, nullptr);
    fr.descriptorSets.clear();
    fr.buffers.clear();
    fr.allocations.clear();
    fr.framebuffers.clear();
}

static void vknvg__renderFlush(void* uptr)
{
    VkNVGcontext* vk = static_cast<VkNVGcontext*>(uptr);

    if (vk->calls.empty() || vk->commandBuffer == VK_NULL_HANDLE || vk->swapchainImageView == VK_NULL_HANDLE)
    {
        vknvg__renderCancel(uptr);
        return;
    }

    // Clean up resources from the previous frame that used this slot.
    // With N frames in flight, slot = frameIndex % kMaxFramesInFlight.
    int slot = static_cast<int>(vk->frameIndex % VkNVGcontext::kMaxFramesInFlight);
    vknvg__cleanupFrameResources(vk, slot);

    int texW = static_cast<int>(vk->viewWidth);
    int texH = static_cast<int>(vk->viewHeight);
    vknvg__ensureStencilImage(vk, texW, texH);

    if (vk->stencilView == VK_NULL_HANDLE)
    {
        vknvg__renderCancel(uptr);
        return;
    }

    // Create vertex buffer
    VkBufferCreateInfo vertBufCI = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    vertBufCI.size = vk->verts.size() * sizeof(NVGvertex);
    vertBufCI.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

    VmaAllocationCreateInfo vertAllocCI = {};
    vertAllocCI.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

    VkBuffer vertBuf = VK_NULL_HANDLE;
    VmaAllocation vertAlloc = VK_NULL_HANDLE;
    if (vmaCreateBuffer(vk->allocator, &vertBufCI, &vertAllocCI, &vertBuf, &vertAlloc, nullptr) != VK_SUCCESS)
    {
        vknvg__renderCancel(uptr);
        return;
    }
    void* vertMapped = nullptr;
    vmaMapMemory(vk->allocator, vertAlloc, &vertMapped);
    memcpy(vertMapped, vk->verts.data(), vk->verts.size() * sizeof(NVGvertex));
    vmaUnmapMemory(vk->allocator, vertAlloc);

    // Create uniform buffer
    if (vk->uniforms.empty())
    {
        DRAXUL_LOG_ERROR(LogCategory::Renderer,
            "NanoVG Vulkan flush produced %zu calls and %zu verts but no uniforms; skipping frame",
            vk->calls.size(), vk->verts.size());
        vmaDestroyBuffer(vk->allocator, vertBuf, vertAlloc);
        vknvg__renderCancel(uptr);
        return;
    }

    VkBufferCreateInfo uniformBufCI = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    uniformBufCI.size = vk->uniforms.size();
    uniformBufCI.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

    VmaAllocationCreateInfo uniformAllocCI = {};
    uniformAllocCI.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

    VkBuffer uniformBuf = VK_NULL_HANDLE;
    VmaAllocation uniformAlloc = VK_NULL_HANDLE;
    if (vmaCreateBuffer(vk->allocator, &uniformBufCI, &uniformAllocCI, &uniformBuf, &uniformAlloc, nullptr) != VK_SUCCESS)
    {
        vmaDestroyBuffer(vk->allocator, vertBuf, vertAlloc);
        vknvg__renderCancel(uptr);
        return;
    }
    void* uniformMapped = nullptr;
    vmaMapMemory(vk->allocator, uniformAlloc, &uniformMapped);
    memcpy(uniformMapped, vk->uniforms.data(), vk->uniforms.size());
    vmaUnmapMemory(vk->allocator, uniformAlloc);

    if (uniformBuf == VK_NULL_HANDLE)
    {
        DRAXUL_LOG_ERROR(LogCategory::Renderer,
            "NanoVG Vulkan created a null uniform buffer (size=%zu, calls=%zu, uniforms=%zu)",
            vk->uniforms.size(), vk->calls.size(), vk->uniforms.size());
        vmaDestroyBuffer(vk->allocator, vertBuf, vertAlloc);
        vknvg__renderCancel(uptr);
        return;
    }

    auto& fr = vk->frameResources[slot];

    VkDescriptorBufferInfo bufInfo = {};
    bufInfo.buffer = uniformBuf;
    bufInfo.offset = 0;
    bufInfo.range = sizeof(VkNVGfragUniforms);

    auto allocDrawDescriptorSet = [&](VkImageView imageView) -> VkDescriptorSet {
        VkDescriptorSet ds = vknvg__allocDescriptorSet(vk);
        if (ds == VK_NULL_HANDLE)
            return VK_NULL_HANDLE;

        VkDescriptorImageInfo imgInfo = {};
        imgInfo.sampler = vk->sampler;
        imgInfo.imageView = imageView;
        imgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet writes[2] = {};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = ds;
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        writes[0].pBufferInfo = &bufInfo;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = ds;
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].pImageInfo = &imgInfo;

        vkUpdateDescriptorSets(vk->device, 2, writes, 0, nullptr);
        fr.descriptorSets.push_back(ds);
        return ds;
    };

    VkDescriptorSet frameDS = allocDrawDescriptorSet(vk->dummyTex.imageView);
    if (frameDS == VK_NULL_HANDLE)
    {
        vmaDestroyBuffer(vk->allocator, vertBuf, vertAlloc);
        vmaDestroyBuffer(vk->allocator, uniformBuf, uniformAlloc);
        vknvg__renderCancel(uptr);
        return;
    }

    // Create per-frame framebuffer
    VkImageView fbAttachments[2] = { vk->swapchainImageView, vk->stencilView };
    VkFramebufferCreateInfo fbCI = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
    fbCI.renderPass = vk->renderPass;
    fbCI.attachmentCount = 2;
    fbCI.pAttachments = fbAttachments;
    fbCI.width = static_cast<uint32_t>(texW);
    fbCI.height = static_cast<uint32_t>(texH);
    fbCI.layers = 1;

    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    if (vkCreateFramebuffer(vk->device, &fbCI, nullptr, &framebuffer) != VK_SUCCESS)
    {
        vmaDestroyBuffer(vk->allocator, vertBuf, vertAlloc);
        vmaDestroyBuffer(vk->allocator, uniformBuf, uniformAlloc);
        vknvg__renderCancel(uptr);
        return;
    }

    // Transition dummy texture to shader read if needed (first time)
    // For simplicity, always transition
    {
        VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.image = vk->dummyTex.image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.layerCount = 1;

        vkCmdPipelineBarrier(vk->commandBuffer,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    // Begin render pass
    VkClearValue clearValues[2] = {};
    clearValues[1].depthStencil = { 0.0f, 0 };

    VkRenderPassBeginInfo rpBegin = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    rpBegin.renderPass = vk->renderPass;
    rpBegin.framebuffer = framebuffer;
    rpBegin.renderArea.extent = { static_cast<uint32_t>(texW), static_cast<uint32_t>(texH) };
    rpBegin.clearValueCount = 2;
    rpBegin.pClearValues = clearValues;

    vkCmdBeginRenderPass(vk->commandBuffer, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

    // Set viewport and scissor
    VkViewport viewport = {};
    viewport.width = vk->viewWidth;
    viewport.height = vk->viewHeight;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(vk->commandBuffer, 0, 1, &viewport);

    VkRect2D scissor = {};
    scissor.extent = { static_cast<uint32_t>(texW), static_cast<uint32_t>(texH) };
    vkCmdSetScissor(vk->commandBuffer, 0, 1, &scissor);

    // Push view size
    float viewSize[2] = { vk->viewWidth, vk->viewHeight };
    vkCmdPushConstants(vk->commandBuffer, vk->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(viewSize), viewSize);

    // Bind vertex buffer
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(vk->commandBuffer, 0, 1, &vertBuf, offsets);

    // Default stencil state
    vkCmdSetStencilReference(vk->commandBuffer, VK_STENCIL_FACE_FRONT_AND_BACK, 0);
    vkCmdSetStencilCompareMask(vk->commandBuffer, VK_STENCIL_FACE_FRONT_AND_BACK, 0xFF);
    vkCmdSetStencilWriteMask(vk->commandBuffer, VK_STENCIL_FACE_FRONT_AND_BACK, 0xFF);

    vk->textureDescriptorSets.clear();

    // Dispatch calls
    for (const auto& call : vk->calls)
    {
        VkDescriptorSet drawDS = frameDS;
        if (call.image != 0)
        {
            VkNVGtexture* t = vknvg__findTexture(vk, call.image);
            if (t && t->imageView != VK_NULL_HANDLE)
            {
                const auto it = vk->textureDescriptorSets.find(call.image);
                if (it != vk->textureDescriptorSets.end())
                {
                    drawDS = it->second;
                }
                else if (VkDescriptorSet textureDS = allocDrawDescriptorSet(t->imageView);
                    textureDS != VK_NULL_HANDLE)
                {
                    vk->textureDescriptorSets.emplace(call.image, textureDS);
                    drawDS = textureDS;
                }
            }
            else
            {
                DRAXUL_LOG_WARN(LogCategory::Renderer,
                    "NanoVG Vulkan could not resolve image %d for a draw call; using dummy texture",
                    call.image);
            }
        }

        switch (call.type)
        {
        case VNVG_CONVEXFILL:
        {
            vkCmdBindPipeline(vk->commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk->pipelineFillAA);

            uint32_t dynOffset = static_cast<uint32_t>(call.uniformOffset);
            vkCmdBindDescriptorSets(vk->commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                vk->pipelineLayout, 0, 1, &drawDS, 1, &dynOffset);

            for (int i = 0; i < call.pathCount; i++)
            {
                const auto& p = vk->paths[call.pathOffset + i];
                if (p.fillCount > 0)
                    vkCmdDraw(vk->commandBuffer, p.fillCount, 1, p.fillOffset, 0);
            }
            // Stroke (AA fringe) — triangle strip
            vkCmdBindPipeline(vk->commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk->pipelineFillNoAA);
            vkCmdBindDescriptorSets(vk->commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                vk->pipelineLayout, 0, 1, &drawDS, 1, &dynOffset);
            for (int i = 0; i < call.pathCount; i++)
            {
                const auto& p = vk->paths[call.pathOffset + i];
                if (p.strokeCount > 0)
                    vkCmdDraw(vk->commandBuffer, p.strokeCount, 1, p.strokeOffset, 0);
            }
            break;
        }
        case VNVG_FILL:
        {
            // Pass 1: stencil fill (no color, both faces)
            vkCmdBindPipeline(vk->commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk->pipelineStencilOnly);
            uint32_t dynOffset = static_cast<uint32_t>(call.uniformOffset);
            vkCmdBindDescriptorSets(vk->commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                vk->pipelineLayout, 0, 1, &drawDS, 1, &dynOffset);

            vkCmdSetStencilWriteMask(vk->commandBuffer, VK_STENCIL_FACE_FRONT_AND_BACK, 0xFF);
            vkCmdSetStencilCompareMask(vk->commandBuffer, VK_STENCIL_FACE_FRONT_AND_BACK, 0xFF);
            vkCmdSetStencilReference(vk->commandBuffer, VK_STENCIL_FACE_FRONT_AND_BACK, 0);

            for (int i = 0; i < call.pathCount; i++)
            {
                const auto& p = vk->paths[call.pathOffset + i];
                if (p.fillCount > 0)
                    vkCmdDraw(vk->commandBuffer, p.fillCount, 1, p.fillOffset, 0);
            }

            // Pass 2: AA fringe (strokes, where stencil == 0)
            int paintOffset = call.uniformOffset + static_cast<int>(sizeof(VkNVGfragUniforms));
            if (vk->flags & NVG_ANTIALIAS)
            {
                vkCmdBindPipeline(vk->commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk->pipelineStencilFringe);
                dynOffset = static_cast<uint32_t>(paintOffset);
                vkCmdBindDescriptorSets(vk->commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    vk->pipelineLayout, 0, 1, &drawDS, 1, &dynOffset);

                for (int i = 0; i < call.pathCount; i++)
                {
                    const auto& p = vk->paths[call.pathOffset + i];
                    if (p.strokeCount > 0)
                        vkCmdDraw(vk->commandBuffer, p.strokeCount, 1, p.strokeOffset, 0);
                }
            }

            // Pass 3: cover (where stencil != 0, zero stencil)
            vkCmdBindPipeline(vk->commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk->pipelineStencilCover);
            dynOffset = static_cast<uint32_t>(paintOffset);
            vkCmdBindDescriptorSets(vk->commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                vk->pipelineLayout, 0, 1, &drawDS, 1, &dynOffset);

            vkCmdSetStencilReference(vk->commandBuffer, VK_STENCIL_FACE_FRONT_AND_BACK, 0);

            if (call.triangleCount > 0)
                vkCmdDraw(vk->commandBuffer, call.triangleCount, 1, call.triangleOffset, 0);

            // Reset stencil state
            vkCmdSetStencilWriteMask(vk->commandBuffer, VK_STENCIL_FACE_FRONT_AND_BACK, 0xFF);
            break;
        }
        case VNVG_STROKE:
        {
            vkCmdBindPipeline(vk->commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk->pipelineFillNoAA);
            uint32_t dynOffset = static_cast<uint32_t>(call.uniformOffset);
            vkCmdBindDescriptorSets(vk->commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                vk->pipelineLayout, 0, 1, &drawDS, 1, &dynOffset);

            for (int i = 0; i < call.pathCount; i++)
            {
                const auto& p = vk->paths[call.pathOffset + i];
                if (p.strokeCount > 0)
                    vkCmdDraw(vk->commandBuffer, p.strokeCount, 1, p.strokeOffset, 0);
            }
            break;
        }
        case VNVG_TRIANGLES:
        {
            vkCmdBindPipeline(vk->commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk->pipelineFillAA);
            uint32_t dynOffset = static_cast<uint32_t>(call.uniformOffset);
            vkCmdBindDescriptorSets(vk->commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                vk->pipelineLayout, 0, 1, &drawDS, 1, &dynOffset);

            if (call.triangleCount > 0)
                vkCmdDraw(vk->commandBuffer, call.triangleCount, 1, call.triangleOffset, 0);
            break;
        }
        default:
            break;
        }
    }

    vkCmdEndRenderPass(vk->commandBuffer);
    fr.buffers.push_back(vertBuf);
    fr.allocations.push_back(vertAlloc);
    fr.buffers.push_back(uniformBuf);
    fr.allocations.push_back(uniformAlloc);
    fr.framebuffers.push_back(framebuffer);

    // Reset deferred state
    vk->calls.clear();
    vk->paths.clear();
    vk->verts.clear();
    vk->uniforms.clear();
}

static void vknvg__renderDelete(void* uptr)
{
    VkNVGcontext* vk = static_cast<VkNVGcontext*>(uptr);

    if (vk->device != VK_NULL_HANDLE)
        vkDeviceWaitIdle(vk->device);

    for (int i = 0; i < VkNVGcontext::kMaxFramesInFlight; i++)
        vknvg__cleanupFrameResources(vk, i);

    vknvg__destroyStencil(vk);
    vknvg__destroyTexture(vk, vk->dummyTex);

    for (auto& tex : vk->textures)
        vknvg__destroyTexture(vk, tex);

    if (vk->pipelineFillAA != VK_NULL_HANDLE)
        vkDestroyPipeline(vk->device, vk->pipelineFillAA, nullptr);
    if (vk->pipelineFillNoAA != VK_NULL_HANDLE)
        vkDestroyPipeline(vk->device, vk->pipelineFillNoAA, nullptr);
    if (vk->pipelineStencilOnly != VK_NULL_HANDLE)
        vkDestroyPipeline(vk->device, vk->pipelineStencilOnly, nullptr);
    if (vk->pipelineStencilFringe != VK_NULL_HANDLE)
        vkDestroyPipeline(vk->device, vk->pipelineStencilFringe, nullptr);
    if (vk->pipelineStencilCover != VK_NULL_HANDLE)
        vkDestroyPipeline(vk->device, vk->pipelineStencilCover, nullptr);
    if (vk->pipelineLayout != VK_NULL_HANDLE)
        vkDestroyPipelineLayout(vk->device, vk->pipelineLayout, nullptr);
    if (vk->descriptorPool != VK_NULL_HANDLE)
        vkDestroyDescriptorPool(vk->device, vk->descriptorPool, nullptr);
    if (vk->descriptorSetLayout != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(vk->device, vk->descriptorSetLayout, nullptr);
    if (vk->sampler != VK_NULL_HANDLE)
        vkDestroySampler(vk->device, vk->sampler, nullptr);
    if (vk->renderPass != VK_NULL_HANDLE)
        vkDestroyRenderPass(vk->device, vk->renderPass, nullptr);
    if (vk->vertModule != VK_NULL_HANDLE)
        vkDestroyShaderModule(vk->device, vk->vertModule, nullptr);
    if (vk->fragModule != VK_NULL_HANDLE)
        vkDestroyShaderModule(vk->device, vk->fragModule, nullptr);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

NVGcontext* nvgCreateVk(VkPhysicalDevice physicalDevice, VkDevice device, VmaAllocator allocator,
    VkFormat colorFormat, int flags)
{
    auto* vk = new VkNVGcontext();
    vk->physicalDevice = physicalDevice;
    vk->device = device;
    vk->allocator = allocator;
    vk->colorFormat = colorFormat;
    vk->flags = flags;

    NVGparams params;
    memset(&params, 0, sizeof(params));
    params.renderCreate = vknvg__renderCreate;
    params.renderCreateTexture = vknvg__renderCreateTexture;
    params.renderDeleteTexture = vknvg__renderDeleteTexture;
    params.renderUpdateTexture = vknvg__renderUpdateTexture;
    params.renderGetTextureSize = vknvg__renderGetTextureSize;
    params.renderViewport = vknvg__renderViewport;
    params.renderCancel = vknvg__renderCancel;
    params.renderFlush = vknvg__renderFlush;
    params.renderFill = vknvg__renderFill;
    params.renderStroke = vknvg__renderStroke;
    params.renderTriangles = vknvg__renderTriangles;
    params.renderDelete = vknvg__renderDelete;
    params.userPtr = vk;
    params.edgeAntiAlias = (flags & NVG_ANTIALIAS) ? 1 : 0;

    NVGcontext* ctx = nvgCreateInternal(&params);
    if (!ctx)
    {
        DRAXUL_LOG_ERROR(LogCategory::Renderer, "NanoVG Vulkan context creation failed");
        delete vk;
        return nullptr;
    }
    return ctx;
}

void nvgDeleteVk(NVGcontext* ctx)
{
    if (!ctx)
        return;
    NVGparams* params = nvgInternalParams(ctx);
    VkNVGcontext* vk = static_cast<VkNVGcontext*>(params->userPtr);
    nvgDeleteInternal(ctx);
    delete vk;
}

void nvgVkSetFrameState(NVGcontext* ctx,
    VkCommandBuffer commandBuffer,
    VkImage swapchainImage,
    VkImageView swapchainImageView,
    uint32_t frameIndex)
{
    NVGparams* params = nvgInternalParams(ctx);
    VkNVGcontext* vk = static_cast<VkNVGcontext*>(params->userPtr);
    vk->commandBuffer = commandBuffer;
    vk->swapchainImage = swapchainImage;
    vk->swapchainImageView = swapchainImageView;
    vk->frameIndex = frameIndex;
}

} // namespace draxul
