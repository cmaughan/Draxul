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
    const BufferResource& get() const { return resource_; }

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
    const ImageResource& get() const { return resource_; }

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

} // namespace draxul::vkresources
