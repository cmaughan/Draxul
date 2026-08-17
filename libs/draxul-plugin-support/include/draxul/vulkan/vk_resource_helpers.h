#pragma once

#include <draxul/vulkan/vk_resource_ownership.h>

#include <filesystem>
#include <string>
#include <string_view>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

namespace draxul::vkresources
{

enum class MemoryPolicy
{
    DevicePreferred,
    HostSequentialWrite,
    HostRandomAccess,
};

enum class LifetimeScope
{
    Persistent,
    Frame,
    Upload,
};

struct BufferRequest
{
    BufferRequest(VkDeviceSize requested_size, VkBufferUsageFlags requested_usage,
        MemoryPolicy requested_memory, std::string_view requested_debug_name,
        LifetimeScope requested_lifetime)
        : size(requested_size)
        , usage(requested_usage)
        , memory(requested_memory)
        , debug_name(requested_debug_name)
        , lifetime(requested_lifetime)
    {
    }

    VkDeviceSize size;
    VkBufferUsageFlags usage;
    MemoryPolicy memory;
    std::string_view debug_name;
    LifetimeScope lifetime;
};

struct ImageRequest
{
    ImageRequest(const VkImageCreateInfo& requested_image, VkImageViewType requested_view_type,
        VkImageAspectFlags requested_aspect, MemoryPolicy requested_memory,
        VkImageLayout requested_final_layout, std::string_view requested_debug_name,
        LifetimeScope requested_lifetime)
        : image(requested_image)
        , view_type(requested_view_type)
        , aspect(requested_aspect)
        , memory(requested_memory)
        , final_layout(requested_final_layout)
        , debug_name(requested_debug_name)
        , lifetime(requested_lifetime)
    {
    }

    VkImageCreateInfo image;
    VkImageViewType view_type;
    VkImageAspectFlags aspect;
    MemoryPolicy memory;
    VkImageLayout final_layout;
    std::string_view debug_name;
    LifetimeScope lifetime;
};

struct BufferResource
{
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    void* mapped = nullptr;
    VkDeviceSize size = 0;
    LifetimeScope lifetime = LifetimeScope::Persistent;
};

struct ImageResource
{
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkImageLayout final_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    LifetimeScope lifetime = LifetimeScope::Persistent;
};

struct ImageTransition
{
    VkImage image;
    VkImageLayout old_layout;
    VkImageLayout new_layout;
    VkAccessFlags src_access;
    VkAccessFlags dst_access;
    VkPipelineStageFlags src_stage;
    VkPipelineStageFlags dst_stage;
    VkImageSubresourceRange range;
};

struct BufferImageUpload
{
    VkBuffer source;
    VkImage destination;
    VkBufferImageCopy copy;
    ImageTransition before_copy;
    ImageTransition after_copy;
    VkImageLayout final_layout;
    std::string_view debug_name;
    LifetimeScope staging_lifetime;
};

class ScopedBuffer
{
public:
    ScopedBuffer() = default;
    ScopedBuffer(const ScopedBuffer&) = delete;
    ScopedBuffer& operator=(const ScopedBuffer&) = delete;
    ScopedBuffer(ScopedBuffer&& other) noexcept;
    ScopedBuffer& operator=(ScopedBuffer&& other) noexcept;
    ~ScopedBuffer();

    BufferResource release();
    const BufferResource& get() const
    {
        return resource_;
    }

private:
    friend bool create_buffer(VkDevice, VmaAllocator, const BufferRequest&, ScopedBuffer&, std::string&);
    void reset();
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    BufferResource resource_{};
};

class ScopedImage
{
public:
    ScopedImage() = default;
    ScopedImage(const ScopedImage&) = delete;
    ScopedImage& operator=(const ScopedImage&) = delete;
    ScopedImage(ScopedImage&& other) noexcept;
    ScopedImage& operator=(ScopedImage&& other) noexcept;
    ~ScopedImage();

    ImageResource release();
    const ImageResource& get() const
    {
        return resource_;
    }

private:
    friend bool create_image(VkDevice, VmaAllocator, const ImageRequest&, ScopedImage&, std::string&);
    void reset();
    VkDevice device_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    ImageResource resource_{};
};

bool create_buffer(VkDevice device, VmaAllocator allocator, const BufferRequest& request,
    ScopedBuffer& output, std::string& error);
bool create_image(VkDevice device, VmaAllocator allocator, const ImageRequest& request,
    ScopedImage& output, std::string& error);
bool create_image_view(VkDevice device, VkImage image, VkFormat format,
    VkImageViewType view_type, const VkImageSubresourceRange& range,
    std::string_view debug_name, VkImageView& output, std::string& error);
VkShaderModule load_shader_module(VkDevice device, const std::filesystem::path& path,
    std::string_view debug_name, std::string& error);
void transition_image(VkCommandBuffer command_buffer, const ImageTransition& transition);
void record_buffer_to_image_upload(VkCommandBuffer command_buffer, const BufferImageUpload& upload);
void destroy_buffer(VmaAllocator allocator, BufferResource& resource);
void destroy_image(VkDevice device, VmaAllocator allocator, ImageResource& resource);

// ---- Render-target attachments ---------------------------------------------
// A single-mip, single-layer 2D framebuffer attachment plus its default view.
// MegaCity and SatView both had a byte-for-byte copy of this; the shared one is
// the only definition now.

struct AttachmentResource
{
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
};

struct AttachmentRequest
{
    AttachmentRequest(int requested_width, int requested_height, VkFormat requested_format,
        VkImageUsageFlags requested_usage, VkImageAspectFlags requested_aspect,
        VkSampleCountFlagBits requested_samples, VkImageCreateFlags requested_flags,
        VkImageLayout requested_final_layout, LifetimeScope requested_lifetime,
        std::string_view requested_debug_name)
        : width(requested_width)
        , height(requested_height)
        , format(requested_format)
        , usage(requested_usage)
        , aspect(requested_aspect)
        , samples(requested_samples)
        , flags(requested_flags)
        , final_layout(requested_final_layout)
        , lifetime(requested_lifetime)
        , debug_name(requested_debug_name)
    {
    }

    int width;
    int height;
    VkFormat format;
    VkImageUsageFlags usage;
    VkImageAspectFlags aspect;
    VkSampleCountFlagBits samples;
    VkImageCreateFlags flags;
    VkImageLayout final_layout;
    LifetimeScope lifetime;
    std::string_view debug_name;
};

bool create_attachment(VkDevice device, VmaAllocator allocator, const AttachmentRequest& request,
    AttachmentResource& output, std::string& error);
// Extra view onto an existing attachment image — used for the mutable-format
// sRGB/UNORM aliasing of the tone-mapped scene target.
bool create_attachment_view(VkDevice device, VkImage image, VkFormat format,
    VkImageAspectFlags aspect, std::string_view debug_name, VkImageView& output,
    std::string& error);
void destroy_attachment(VkDevice device, VmaAllocator allocator, AttachmentResource& resource);

// ---- MSAA sample-count selection -------------------------------------------
// Probes the ACTUAL per-format sample support for the color and depth formats a
// scene pass will use, walking 4x -> 2x -> 1x. This replaces MegaCity's
// limits-only "4 or 1" query (audit bug #7), which could report 4x for formats
// the device does not support multisampled and had no 2x fallback rung.
bool format_supports_samples(VkPhysicalDevice physical_device, VkFormat format,
    VkImageUsageFlags usage, VkSampleCountFlagBits samples);
VkSampleCountFlagBits choose_scene_sample_count(VkPhysicalDevice physical_device,
    VkFormat color_format, VkFormat depth_format);

// ---- Shaders ----------------------------------------------------------------
// Convenience wrapper over load_shader_module: derives the debug name from the
// file name using `prefix`, and logs the failure itself.
VkShaderModule load_shader(VkDevice device, const std::filesystem::path& path,
    std::string_view log_prefix, bool required = true);

// ---- Immediate (queue-blocking) image upload --------------------------------
// One-shot upload used at load time by both products: allocate a staging buffer,
// copy, submit on the graphics queue, wait idle, destroy staging.

struct SampledImageResource
{
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    int width = 0;
    int height = 0;
    uint32_t mip_levels = 1;
};

struct SampledImageRequest
{
    SampledImageRequest(int requested_width, int requested_height, VkFormat requested_format,
        VkSamplerAddressMode requested_address_mode, bool requested_generate_mips,
        VkImageLayout requested_final_layout, LifetimeScope requested_lifetime,
        std::string_view requested_debug_name)
        : width(requested_width)
        , height(requested_height)
        , format(requested_format)
        , address_mode(requested_address_mode)
        , generate_mips(requested_generate_mips)
        , final_layout(requested_final_layout)
        , lifetime(requested_lifetime)
        , debug_name(requested_debug_name)
    {
    }

    int width;
    int height;
    VkFormat format;
    VkSamplerAddressMode address_mode;
    bool generate_mips;
    VkImageLayout final_layout;
    LifetimeScope lifetime;
    std::string_view debug_name;
};

// Creates a sampled 2D image + view + linear sampler (anisotropic when the
// device supports it). Mip levels are derived from the extent when
// `generate_mips` is set; the caller uploads level 0 and blits the chain.
bool create_sampled_image(VkPhysicalDevice physical_device, VkDevice device, VmaAllocator allocator,
    const SampledImageRequest& request, SampledImageResource& output, std::string& error);
void destroy_sampled_image(VkDevice device, VmaAllocator allocator, SampledImageResource& resource);

// Handles for a one-shot upload submission. `queue_family` must own `queue`.
struct ImmediateUploadContext
{
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t queue_family = 0;
};

// Uploads tightly packed `pixels` (bytes_per_pixel * width * height) into
// `target` level 0 and leaves it in `target.final_layout`-equivalent
// SHADER_READ_ONLY_OPTIMAL, generating mips when the image has them. Blocks on
// the queue: load-time only.
bool upload_image_immediate(const ImmediateUploadContext& ctx, const void* pixels,
    size_t byte_size, SampledImageResource& target, VkImageLayout old_layout,
    std::string& error);

} // namespace draxul::vkresources
