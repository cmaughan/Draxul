#include "vk_context.h"

#include <SDL3/SDL_vulkan.h>
#include <VkBootstrap.h>
#include <draxul/log.h>

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

namespace draxul
{

namespace
{

void destroy_swapchain_info(VkDevice device, VmaAllocator allocator, SwapchainInfo& swapchain)
{
    for (auto fb : swapchain.framebuffers)
        vkDestroyFramebuffer(device, fb, nullptr);
    for (auto iv : swapchain.depth_image_views)
        vkDestroyImageView(device, iv, nullptr);
    for (size_t i = 0; i < swapchain.depth_images.size(); ++i)
    {
        if (swapchain.depth_images[i] != VK_NULL_HANDLE)
            vmaDestroyImage(allocator, swapchain.depth_images[i], swapchain.depth_allocations[i]);
    }
    for (auto iv : swapchain.image_views)
        vkDestroyImageView(device, iv, nullptr);
    if (swapchain.swapchain != VK_NULL_HANDLE)
        vkDestroySwapchainKHR(device, swapchain.swapchain, nullptr);
    swapchain = {};
}

} // namespace

bool VkContext::initialize(SDL_Window* window)
{
    window_ = window;

    vkb::InstanceBuilder instance_builder;
    auto inst_ret = instance_builder
                        .set_app_name("draxul")
                        .set_engine_name("draxul")
                        .require_api_version(1, 2, 0)
#ifndef NDEBUG
                        .request_validation_layers()
                        .use_default_debug_messenger()
#endif
                        .build();

    if (!inst_ret)
    {
        DRAXUL_LOG_ERROR(LogCategory::Renderer, "Failed to create Vulkan instance: %s", inst_ret.error().message().c_str());
        return false;
    }

    auto vkb_inst = inst_ret.value();
    instance_ = vkb_inst.instance;
    debug_messenger_ = vkb_inst.debug_messenger;

    if (!SDL_Vulkan_CreateSurface(window, instance_, nullptr, &surface_))
    {
        DRAXUL_LOG_ERROR(LogCategory::Renderer, "Failed to create Vulkan surface: %s", SDL_GetError());
        return false;
    }

    vkb::PhysicalDeviceSelector selector(vkb_inst);
    auto phys_ret = selector
                        .set_surface(surface_)
                        .set_minimum_version(1, 2)
                        .prefer_gpu_device_type(vkb::PreferredDeviceType::discrete)
                        .select();
    if (!phys_ret)
    {
        DRAXUL_LOG_ERROR(LogCategory::Renderer, "Failed to select physical device: %s", phys_ret.error().message().c_str());
        return false;
    }

    auto vkb_phys = phys_ret.value();
    physical_device_ = vkb_phys.physical_device;

    vkb::DeviceBuilder device_builder(vkb_phys);
    auto dev_ret = device_builder.build();
    if (!dev_ret)
    {
        DRAXUL_LOG_ERROR(LogCategory::Renderer, "Failed to create logical device: %s", dev_ret.error().message().c_str());
        return false;
    }

    auto vkb_dev = dev_ret.value();
    device_ = vkb_dev.device;

    auto queue_ret = vkb_dev.get_queue(vkb::QueueType::graphics);
    if (!queue_ret)
    {
        DRAXUL_LOG_ERROR(LogCategory::Renderer, "Failed to get graphics queue");
        return false;
    }
    graphics_queue_ = queue_ret.value();
    graphics_queue_family_ = vkb_dev.get_queue_index(vkb::QueueType::graphics).value();

    VmaAllocatorCreateInfo alloc_info = {};
    alloc_info.physicalDevice = physical_device_;
    alloc_info.device = device_;
    alloc_info.instance = instance_;
    alloc_info.vulkanApiVersion = VK_API_VERSION_1_2;
    if (vmaCreateAllocator(&alloc_info, &allocator_) != VK_SUCCESS)
    {
        DRAXUL_LOG_ERROR(LogCategory::Renderer, "Failed to create VMA allocator");
        return false;
    }

    int w = 0;
    int h = 0;
    SDL_GetWindowSizeInPixels(window, &w, &h);
    return recreate_swapchain(w, h);
}

VkFormat VkContext::choose_depth_format() const
{
    constexpr VkFormat candidates[] = {
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D16_UNORM,
    };

    for (VkFormat format : candidates)
    {
        VkFormatProperties props{};
        vkGetPhysicalDeviceFormatProperties(physical_device_, format, &props);
        if ((props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0)
            return format;
    }

    return VK_FORMAT_UNDEFINED;
}

bool VkContext::create_render_pass(VkFormat color_format, VkFormat depth_format, VkRenderPass& render_pass)
{
    VkAttachmentDescription color_attachment = {};
    color_attachment.format = color_format;
    color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color_attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription depth_attachment = {};
    depth_attachment.format = depth_format;
    depth_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depth_attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference color_ref = {};
    color_ref.attachment = 0;
    color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depth_ref = {};
    depth_ref.attachment = 1;
    depth_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_ref;
    subpass.pDepthStencilAttachment = &depth_ref;

    VkSubpassDependency dependency = {};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkAttachmentDescription attachments[2] = { color_attachment, depth_attachment };

    VkRenderPassCreateInfo rp_info = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    rp_info.attachmentCount = 2;
    rp_info.pAttachments = attachments;
    rp_info.subpassCount = 1;
    rp_info.pSubpasses = &subpass;
    rp_info.dependencyCount = 1;
    rp_info.pDependencies = &dependency;

    if (vkCreateRenderPass(device_, &rp_info, nullptr, &render_pass) != VK_SUCCESS)
    {
        DRAXUL_LOG_ERROR(LogCategory::Renderer, "Failed to create render pass");
        return false;
    }

    return true;
}

bool VkContext::create_depth_resources(SwapchainInfo& swapchain)
{
    swapchain.depth_images.clear();
    swapchain.depth_allocations.clear();
    swapchain.depth_image_views.clear();
    swapchain.depth_images.reserve(swapchain.image_views.size());
    swapchain.depth_allocations.reserve(swapchain.image_views.size());
    swapchain.depth_image_views.reserve(swapchain.image_views.size());

    for (size_t i = 0; i < swapchain.image_views.size(); ++i)
    {
        VkImageCreateInfo img_ci = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        img_ci.imageType = VK_IMAGE_TYPE_2D;
        img_ci.format = swapchain.depth_format;
        img_ci.extent = { swapchain.extent.width, swapchain.extent.height, 1 };
        img_ci.mipLevels = 1;
        img_ci.arrayLayers = 1;
        img_ci.samples = VK_SAMPLE_COUNT_1_BIT;
        img_ci.tiling = VK_IMAGE_TILING_OPTIMAL;
        img_ci.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        img_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        img_ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VmaAllocationCreateInfo alloc_ci = {};
        alloc_ci.usage = VMA_MEMORY_USAGE_AUTO;

        VkImage image = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        if (vmaCreateImage(allocator_, &img_ci, &alloc_ci, &image, &allocation, nullptr) != VK_SUCCESS)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "Failed to create Vulkan depth image");
            return false;
        }

        VkImageViewCreateInfo view_ci = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        view_ci.image = image;
        view_ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_ci.format = swapchain.depth_format;
        view_ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        view_ci.subresourceRange.levelCount = 1;
        view_ci.subresourceRange.layerCount = 1;

        VkImageView image_view = VK_NULL_HANDLE;
        if (vkCreateImageView(device_, &view_ci, nullptr, &image_view) != VK_SUCCESS)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "Failed to create Vulkan depth image view");
            vmaDestroyImage(allocator_, image, allocation);
            return false;
        }

        swapchain.depth_images.push_back(image);
        swapchain.depth_allocations.push_back(allocation);
        swapchain.depth_image_views.push_back(image_view);
    }

    return true;
}

bool VkContext::recreate_swapchain(int width, int height)
{
    PendingSwapchainResources pending;
    if (!build_swapchain_resources(width, height, pending))
        return false;

    commit_swapchain_resources(std::move(pending));
    return true;
}

bool VkContext::build_swapchain_resources(int width, int height, PendingSwapchainResources& pending)
{
    destroy_pending_swapchain_resources(pending);

    vkb::SwapchainBuilder sc_builder(physical_device_, device_, surface_);
    auto sc_ret = sc_builder
                      .set_desired_format({ VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR })
                      .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
                      .set_desired_extent(width, height)
                      .add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
                      .set_old_swapchain(swapchain_.swapchain)
                      .build();

    if (!sc_ret)
    {
        DRAXUL_LOG_ERROR(LogCategory::Renderer, "Failed to create swapchain: %s", sc_ret.error().message().c_str());
        return false;
    }

    auto vkb_sc = sc_ret.value();
    pending.swapchain.swapchain = vkb_sc.swapchain;
    pending.swapchain.format = vkb_sc.image_format;
    pending.swapchain.extent = vkb_sc.extent;

    auto images_ret = vkb_sc.get_images();
    if (!images_ret)
    {
        DRAXUL_LOG_ERROR(LogCategory::Renderer, "Failed to get swapchain images: %s", images_ret.error().message().c_str());
        destroy_pending_swapchain_resources(pending);
        return false;
    }
    pending.swapchain.images = images_ret.value();

    auto views_ret = vkb_sc.get_image_views();
    if (!views_ret)
    {
        DRAXUL_LOG_ERROR(LogCategory::Renderer, "Failed to get swapchain image views: %s", views_ret.error().message().c_str());
        destroy_pending_swapchain_resources(pending);
        return false;
    }
    pending.swapchain.image_views = views_ret.value();

    pending.swapchain.depth_format = choose_depth_format();
    if (pending.swapchain.depth_format == VK_FORMAT_UNDEFINED)
    {
        DRAXUL_LOG_ERROR(LogCategory::Renderer, "Failed to find a supported Vulkan depth format");
        destroy_pending_swapchain_resources(pending);
        return false;
    }

    if (!create_render_pass(pending.swapchain.format, pending.swapchain.depth_format, pending.render_pass))
    {
        destroy_pending_swapchain_resources(pending);
        return false;
    }

    if (!create_depth_resources(pending.swapchain))
    {
        destroy_pending_swapchain_resources(pending);
        return false;
    }

    if (!create_framebuffers(pending.swapchain, pending.render_pass))
    {
        destroy_pending_swapchain_resources(pending);
        return false;
    }

    return true;
}

void VkContext::commit_swapchain_resources(PendingSwapchainResources&& pending)
{
    vkDeviceWaitIdle(device_);

    destroy_swapchain();
    if (render_pass_ != VK_NULL_HANDLE)
    {
        vkDestroyRenderPass(device_, render_pass_, nullptr);
        render_pass_ = VK_NULL_HANDLE;
    }

    swapchain_ = std::move(pending.swapchain);
    render_pass_ = pending.render_pass;
    pending.render_pass = VK_NULL_HANDLE;
}

bool VkContext::create_framebuffers(SwapchainInfo& swapchain, VkRenderPass render_pass)
{
    swapchain.framebuffers.clear();
    swapchain.framebuffers.reserve(swapchain.image_views.size());

    for (size_t i = 0; i < swapchain.image_views.size(); i++)
    {
        VkImageView attachments[2] = {
            swapchain.image_views[i],
            swapchain.depth_image_views[i],
        };
        VkFramebufferCreateInfo fb_info = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
        fb_info.renderPass = render_pass;
        fb_info.attachmentCount = 2;
        fb_info.pAttachments = attachments;
        fb_info.width = swapchain.extent.width;
        fb_info.height = swapchain.extent.height;
        fb_info.layers = 1;

        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        if (vkCreateFramebuffer(device_, &fb_info, nullptr, &framebuffer) != VK_SUCCESS)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "Failed to create framebuffer");
            for (auto existing : swapchain.framebuffers)
                vkDestroyFramebuffer(device_, existing, nullptr);
            swapchain.framebuffers.clear();
            return false;
        }

        swapchain.framebuffers.push_back(framebuffer);
    }

    return true;
}

void VkContext::destroy_pending_swapchain_resources(PendingSwapchainResources& pending)
{
    if (pending.render_pass != VK_NULL_HANDLE)
    {
        vkDestroyRenderPass(device_, pending.render_pass, nullptr);
        pending.render_pass = VK_NULL_HANDLE;
    }

    destroy_swapchain_info(device_, allocator_, pending.swapchain);
}

void VkContext::destroy_swapchain()
{
    destroy_swapchain_info(device_, allocator_, swapchain_);
}

void VkContext::shutdown()
{
    if (device_)
        vkDeviceWaitIdle(device_);

    destroy_swapchain();
    if (render_pass_)
        vkDestroyRenderPass(device_, render_pass_, nullptr);
    if (allocator_)
        vmaDestroyAllocator(allocator_);
    if (device_)
        vkDestroyDevice(device_, nullptr);
    if (surface_)
        vkDestroySurfaceKHR(instance_, surface_, nullptr);

    if (instance_ && debug_messenger_)
        vkb::destroy_debug_utils_messenger(instance_, debug_messenger_);
    if (instance_)
        vkDestroyInstance(instance_, nullptr);
}

} // namespace draxul
