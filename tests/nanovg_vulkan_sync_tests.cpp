#include "support/test_support.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

#if defined(_WIN32)
#include "vulkan/vk_renderer.h"
#include "vulkan/vk_renderer_operations.h"

#include <cstdint>
#include <type_traits>
#include <vector>
#endif

namespace
{

// Shared read_file plus the CR normalization these shader/CMake text
// comparisons need on Windows checkouts.
std::string read_text_file(const std::filesystem::path& path)
{
    std::string text = draxul::tests::read_file(path);
    text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
    return text;
}

} // namespace

#if defined(_WIN32)

namespace
{

template <typename Handle>
Handle fake_handle(uintptr_t value)
{
    if constexpr (std::is_pointer_v<Handle>)
        return reinterpret_cast<Handle>(value);
    else
        return static_cast<Handle>(value);
}

enum class FailurePoint
{
    None,
    End,
    Submit,
    Allocate,
    Reset,
    Begin,
    ResetFence,
};

class RecordingVulkanOperations final : public draxul::VkRendererOperations
{
public:
    VkResult allocate_command_buffers(VkDevice, const VkCommandBufferAllocateInfo*,
        VkCommandBuffer* buffers) override
    {
        ++operation_count;
        if (failure == FailurePoint::Allocate)
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        *buffers = fake_handle<VkCommandBuffer>(0x101);
        return VK_SUCCESS;
    }
    VkResult reset_command_buffer(VkCommandBuffer, VkCommandBufferResetFlags) override
    {
        ++operation_count;
        return failure == FailurePoint::Reset ? VK_ERROR_UNKNOWN : VK_SUCCESS;
    }
    VkResult begin_command_buffer(VkCommandBuffer, const VkCommandBufferBeginInfo*) override
    {
        ++operation_count;
        return failure == FailurePoint::Begin ? VK_ERROR_UNKNOWN : VK_SUCCESS;
    }
    VkResult end_command_buffer(VkCommandBuffer) override
    {
        ++operation_count;
        return failure == FailurePoint::End ? VK_ERROR_UNKNOWN : VK_SUCCESS;
    }
    VkResult queue_submit(VkQueue, uint32_t, const VkSubmitInfo*, VkFence) override
    {
        ++operation_count;
        return failure == FailurePoint::Submit ? VK_ERROR_DEVICE_LOST : VK_SUCCESS;
    }
    VkResult reset_fences(VkDevice, uint32_t, const VkFence*) override
    {
        ++operation_count;
        return failure == FailurePoint::ResetFence ? VK_ERROR_DEVICE_LOST : VK_SUCCESS;
    }
    VkResult wait_for_fences(VkDevice, uint32_t, const VkFence*, VkBool32, uint64_t) override
    {
        ++operation_count;
        return VK_SUCCESS;
    }
    VkResult device_wait_idle(VkDevice) override
    {
        ++device_idle_count;
        return VK_SUCCESS;
    }
    VkResult create_semaphore(VkDevice, const VkSemaphoreCreateInfo*, const VkAllocationCallbacks*,
        VkSemaphore* semaphore) override
    {
        *semaphore = replacement_semaphore;
        return VK_SUCCESS;
    }
    VkResult create_fence(VkDevice, const VkFenceCreateInfo* info, const VkAllocationCallbacks*,
        VkFence* fence) override
    {
        replacement_fence_signaled = (info->flags & VK_FENCE_CREATE_SIGNALED_BIT) != 0;
        *fence = replacement_fence;
        return VK_SUCCESS;
    }
    void destroy_semaphore(VkDevice, VkSemaphore, const VkAllocationCallbacks*) override { }
    void destroy_fence(VkDevice, VkFence, const VkAllocationCallbacks*) override { }
    void cmd_pipeline_barrier(VkCommandBuffer, VkPipelineStageFlags, VkPipelineStageFlags,
        VkDependencyFlags, uint32_t, const VkMemoryBarrier*, uint32_t,
        const VkBufferMemoryBarrier*, uint32_t, const VkImageMemoryBarrier*) override
    {
    }
    void cmd_copy_image_to_buffer(VkCommandBuffer, VkImage, VkImageLayout, VkBuffer,
        uint32_t, const VkBufferImageCopy*) override
    {
    }
    VkResult queue_present(VkQueue, const VkPresentInfoKHR*) override { return VK_SUCCESS; }
    VkResult invalidate_allocation(VmaAllocator, VmaAllocation, VkDeviceSize, VkDeviceSize size) override
    {
        invalidated_size = size;
        return invalidate_result;
    }

    FailurePoint failure = FailurePoint::None;
    int operation_count = 0;
    int device_idle_count = 0;
    bool replacement_fence_signaled = false;
    VkDeviceSize invalidated_size = 0;
    VkResult invalidate_result = VK_SUCCESS;
    VkSemaphore replacement_semaphore = fake_handle<VkSemaphore>(0x201);
    VkFence replacement_fence = fake_handle<VkFence>(0x202);
};

} // namespace

namespace draxul
{

struct VkRendererTestAccess
{
    struct State
    {
        bool frame_active;
        bool has_active_command_buffer;
        bool rebuild_pending;
        bool renderer_failed;
        uint32_t chunk_index;
        size_t extra_count;
        VkFence image_fence;
        VkFence frame_fence;
        VkSemaphore image_available;
    };

    static std::unique_ptr<VkRenderer> make(VkRendererOperations& operations)
    {
        return std::unique_ptr<VkRenderer>(
            new VkRenderer(kAtlasSize, {}, operations));
    }

    static void arm_frame(VkRenderer& renderer, bool reuse_extra = false)
    {
        renderer.current_frame_ = 0;
        renderer.current_image_ = 0;
        renderer.frame_active_ = true;
        renderer.active_cmd_buffer_ = fake_handle<VkCommandBuffer>(0x301);
        renderer.chunk_has_work_ = true;
        renderer.current_chunk_index_ = 0;
        renderer.framebuffer_resized_ = false;
        renderer.renderer_failed_ = false;
        renderer.image_available_sem_ = { fake_handle<VkSemaphore>(0x401) };
        renderer.render_finished_sem_ = { fake_handle<VkSemaphore>(0x402) };
        renderer.in_flight_fences_ = { fake_handle<VkFence>(0x403) };
        renderer.images_in_flight_ = { renderer.in_flight_fences_[0] };
        auto& swapchain
            = const_cast<SwapchainInfo&>(renderer.ctx_.swapchain());
        swapchain.images = { fake_handle<VkImage>(0x405) };
        renderer.extra_cmd_buffers_[0].clear();
        if (reuse_extra)
            renderer.extra_cmd_buffers_[0].push_back(
                fake_handle<VkCommandBuffer>(0x404));
    }

    static bool flush(VkRenderer& renderer, bool final_chunk)
    {
        return renderer.flush_submit_chunk(final_chunk);
    }

    static State state(const VkRenderer& renderer)
    {
        return { renderer.frame_active_,
            renderer.active_cmd_buffer_ != VK_NULL_HANDLE,
            renderer.framebuffer_resized_, renderer.renderer_failed_,
            renderer.current_chunk_index_,
            renderer.extra_cmd_buffers_[0].size(),
            renderer.images_in_flight_[0], renderer.in_flight_fences_[0],
            renderer.image_available_sem_[0] };
    }

    static void arm_capture(VkRenderer& renderer, void* bytes, size_t buffer_size,
        uint32_t width, uint32_t height, size_t byte_count)
    {
        renderer.capture_requested_ = true;
        renderer.capture_buffer_ = fake_handle<VkBuffer>(0x501);
        renderer.capture_allocation_ = fake_handle<VmaAllocation>(0x502);
        renderer.capture_mapped_ = bytes;
        renderer.capture_buffer_size_ = buffer_size;
        renderer.capture_width_ = width;
        renderer.capture_height_ = height;
        renderer.capture_byte_count_ = byte_count;
    }

    static void finish_capture(VkRenderer& renderer)
    {
        renderer.finish_capture_readback();
    }
};

} // namespace draxul

#endif

TEST_CASE("NanoVG Vulkan textures use tracked transfer uploads", "[nanovg][vulkan][sync]")
{
    const auto source = read_text_file(
        std::filesystem::path(DRAXUL_PROJECT_ROOT)
        / "libs" / "draxul-nanovg" / "backend" / "src" / "nanovg_vk.cpp");
    REQUIRE_FALSE(source.empty());

    CHECK(source.find("VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED") != std::string::npos);
    CHECK(source.find("imgCI.tiling = VK_IMAGE_TILING_LINEAR") == std::string::npos);
    CHECK(source.find("toTransfer.oldLayout = tex.layout") != std::string::npos);
    CHECK(source.find("VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy") != std::string::npos);
    CHECK(source.find("toSampled.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL") != std::string::npos);
    CHECK(source.find("toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED") != std::string::npos);
    CHECK(source.find("vmaFlushAllocation(vk->allocator, allocation") != std::string::npos);

    // UNDEFINED is valid only for a texture's first upload. The old backend's
    // unconditional per-frame dummy transition discarded its white texel.
    CHECK(source.find("barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED") == std::string::npos);
}

TEST_CASE("NanoVG Vulkan frame resources follow the renderer contract", "[nanovg][vulkan][frames]")
{
    const auto source = read_text_file(
        std::filesystem::path(DRAXUL_PROJECT_ROOT)
        / "libs" / "draxul-nanovg" / "backend" / "src" / "nanovg_vk.cpp");
    const auto pass = read_text_file(
        std::filesystem::path(DRAXUL_PROJECT_ROOT)
        / "libs" / "draxul-nanovg" / "src" / "nanovg_pass_vk.cpp");
    REQUIRE_FALSE(source.empty());
    REQUIRE_FALSE(pass.empty());

    CHECK(source.find("kMaxFramesInFlight") == std::string::npos);
    CHECK(source.find("frameResources.resize(requestedCount)") != std::string::npos);
    CHECK(source.find("frameIndex % vk->frameResources.size()") != std::string::npos);
    CHECK(pass.find("vk_ctx.buffered_frame_count()") != std::string::npos);
}

TEST_CASE("Vulkan presentation semaphores follow acquired swapchain images", "[nanovg][vulkan][present]")
{
    const auto source = read_text_file(
        std::filesystem::path(DRAXUL_PROJECT_ROOT)
        / "libs" / "draxul-renderer" / "src" / "vulkan" / "vk_renderer.cpp");
    REQUIRE_FALSE(source.empty());

    CHECK(source.find("resize_render_finished_semaphores(ctx_.swapchain().images.size())") != std::string::npos);
    CHECK(source.find("pSignalSemaphores = &render_finished_sem_[current_image_]") != std::string::npos);
    CHECK(source.find("pWaitSemaphores = &render_finished_sem_[current_image_]") != std::string::npos);
    CHECK(source.find("pSignalSemaphores = &render_finished_sem_[current_frame_]") == std::string::npos);
}

TEST_CASE("Vulkan capture readback keeps the recorded image extent", "[nanovg][vulkan][capture]")
{
    const auto source = read_text_file(
        std::filesystem::path(DRAXUL_PROJECT_ROOT)
        / "libs" / "draxul-renderer" / "src" / "vulkan" / "vk_renderer.cpp");
    const auto header = read_text_file(
        std::filesystem::path(DRAXUL_PROJECT_ROOT)
        / "libs" / "draxul-renderer" / "src" / "vulkan" / "vk_renderer.h");
    REQUIRE_FALSE(source.empty());
    REQUIRE_FALSE(header.empty());

    CHECK(header.find("uint32_t capture_width_") != std::string::npos);
    CHECK(header.find("uint32_t capture_height_") != std::string::npos);
    CHECK(header.find("size_t capture_byte_count_") != std::string::npos);
    CHECK(source.find("frame.width = static_cast<int>(capture_width_)") != std::string::npos);
    CHECK(source.find("frame.height = static_cast<int>(capture_height_)") != std::string::npos);

    const auto capture_readback = source.find("finish_capture_readback();");
    const auto present = source.find(
        "operations_.queue_present(ctx_.graphics_queue(), &present)",
        capture_readback);
    REQUIRE(capture_readback != std::string::npos);
    REQUIRE(present != std::string::npos);
    CHECK(capture_readback < present);

    const auto metal = read_text_file(
        std::filesystem::path(DRAXUL_PROJECT_ROOT)
        / "libs" / "draxul-renderer" / "src" / "metal" / "metal_renderer.mm");
    REQUIRE_FALSE(metal.empty());
    CHECK(metal.find("capture_width_ = width") != std::string::npos);
    CHECK(metal.find("capture_height_ = height") != std::string::npos);
    CHECK(metal.find("frame.width = static_cast<int>(capture_width_)") != std::string::npos);
    CHECK(metal.find("frame.height = static_cast<int>(capture_height_)") != std::string::npos);
}

TEST_CASE("Vulkan chunk failures retire frame ownership before another acquire", "[nanovg][vulkan][recovery]")
{
    const auto source = read_text_file(
        std::filesystem::path(DRAXUL_PROJECT_ROOT)
        / "libs" / "draxul-renderer" / "src" / "vulkan" / "vk_renderer.cpp");
    REQUIRE_FALSE(source.empty());

    CHECK(source.find("void VkRenderer::abort_active_frame()") != std::string::npos);
    CHECK(source.find("bool VkRenderer::replace_current_frame_sync_objects()") != std::string::npos);
    CHECK(source.find("if (!start_new_chunk_command_buffer())\n        return abort_after_failure();") != std::string::npos);

    const auto submit = source.find("operations_.queue_submit(ctx_.graphics_queue(), 1, &submit,");
    const auto associate = source.find("images_in_flight_[current_image_] = in_flight_fences_[current_frame_]", submit);
    REQUIRE(submit != std::string::npos);
    REQUIRE(associate != std::string::npos);
    CHECK(submit < associate);
}

#if defined(_WIN32)

TEST_CASE("Vulkan chunk transitions abort atomically at every fallible operation",
    "[nanovg][vulkan][recovery][failure-injection]")
{
    struct FailureCase
    {
        FailurePoint point;
        bool reuse_extra;
        size_t expected_extra_count;
    };
    constexpr FailureCase cases[] = {
        { FailurePoint::End, false, 0 },
        { FailurePoint::Submit, false, 0 },
        { FailurePoint::Allocate, false, 0 },
        { FailurePoint::Reset, true, 1 },
        { FailurePoint::Begin, true, 1 },
    };

    for (const auto& test_case : cases)
    {
        DYNAMIC_SECTION("failure point " << static_cast<int>(test_case.point))
        {
            RecordingVulkanOperations operations;
            operations.failure = test_case.point;
            auto renderer = draxul::VkRendererTestAccess::make(operations);
            draxul::VkRendererTestAccess::arm_frame(
                *renderer, test_case.reuse_extra);

            CHECK_FALSE(draxul::VkRendererTestAccess::flush(*renderer, false));
            const auto failed = draxul::VkRendererTestAccess::state(*renderer);
            CHECK_FALSE(failed.frame_active);
            CHECK_FALSE(failed.has_active_command_buffer);
            CHECK(failed.rebuild_pending);
            CHECK_FALSE(failed.renderer_failed);
            CHECK(failed.chunk_index == 0);
            CHECK(failed.extra_count == test_case.expected_extra_count);
            CHECK(failed.image_fence == VK_NULL_HANDLE);
            CHECK(failed.frame_fence == operations.replacement_fence);
            CHECK(failed.image_available == operations.replacement_semaphore);
            CHECK(operations.replacement_fence_signaled);
            CHECK(operations.device_idle_count == 1);

            const int calls_after_abort = operations.operation_count;
            CHECK_FALSE(draxul::VkRendererTestAccess::flush(*renderer, false));
            CHECK(operations.operation_count == calls_after_abort);
        }
    }
}

TEST_CASE("Vulkan final-submit failure retires unsignaled frame ownership",
    "[nanovg][vulkan][recovery][failure-injection]")
{
    RecordingVulkanOperations operations;
    operations.failure = FailurePoint::Submit;
    auto renderer = draxul::VkRendererTestAccess::make(operations);
    draxul::VkRendererTestAccess::arm_frame(*renderer);

    CHECK_FALSE(draxul::VkRendererTestAccess::flush(*renderer, true));
    const auto failed = draxul::VkRendererTestAccess::state(*renderer);
    CHECK_FALSE(failed.frame_active);
    CHECK_FALSE(failed.has_active_command_buffer);
    CHECK(failed.image_fence == VK_NULL_HANDLE);
    CHECK(failed.frame_fence == operations.replacement_fence);
    CHECK(failed.image_available == operations.replacement_semaphore);
    CHECK(operations.replacement_fence_signaled);
    CHECK(operations.device_idle_count == 1);
}

TEST_CASE("Vulkan capture readback rejects allocation overruns and preserves recorded dimensions",
    "[nanovg][vulkan][capture][failure-injection]")
{
    RecordingVulkanOperations operations;
    auto renderer = draxul::VkRendererTestAccess::make(operations);
    const std::array<uint8_t, 8> bgra = { 3, 2, 1, 4, 7, 6, 5, 8 };
    draxul::VkRendererTestAccess::arm_capture(
        *renderer, const_cast<uint8_t*>(bgra.data()), bgra.size(), 2, 1, 8);

    draxul::VkRendererTestAccess::finish_capture(*renderer);
    auto capture = renderer->take_captured_frame();
    REQUIRE(capture.has_value());
    CHECK(capture->width == 2);
    CHECK(capture->height == 1);
    const std::vector<uint8_t> expected_rgba{ 1, 2, 3, 4, 5, 6, 7, 8 };
    CHECK(capture->rgba == expected_rgba);
    CHECK(operations.invalidated_size == bgra.size());

    draxul::VkRendererTestAccess::arm_capture(
        *renderer, const_cast<uint8_t*>(bgra.data()), bgra.size(), 3, 1, 12);
    operations.invalidated_size = 0;
    draxul::VkRendererTestAccess::finish_capture(*renderer);
    CHECK_FALSE(renderer->take_captured_frame().has_value());
    CHECK(operations.invalidated_size == 0);
}

#endif

TEST_CASE("NanoVG dependency is immutable", "[nanovg][dependency]")
{
    // The pin lives beside the backend so standalone product extractions of
    // libs/draxul-nanovg fetch the identical source.
    const auto cmake = read_text_file(
        std::filesystem::path(DRAXUL_PROJECT_ROOT)
        / "libs" / "draxul-nanovg" / "CMakeLists.txt");
    REQUIRE_FALSE(cmake.empty());

    const auto declaration = cmake.find("FetchContent_Declare(");
    REQUIRE(declaration != std::string::npos);
    const auto end = cmake.find(')', declaration);
    REQUIRE(end != std::string::npos);
    const auto block = cmake.substr(declaration, end - declaration);
    CHECK(block.find("nanovg") != std::string::npos);
    CHECK(block.find("GIT_TAG ce3bf745eb2d2dbc14a50bf2446783f691ac4353") != std::string::npos);
    CHECK(block.find("GIT_TAG master") == std::string::npos);

    // And it must not quietly reappear in the central fetch list.
    const auto central = read_text_file(
        std::filesystem::path(DRAXUL_PROJECT_ROOT) / "cmake" / "FetchDependencies.cmake");
    REQUIRE_FALSE(central.empty());
    CHECK(central.find("FetchContent_Declare(\n    nanovg") == std::string::npos);
}

TEST_CASE("NanoVG Metal creation gives failed contexts one backend owner", "[nanovg][metal][ownership]")
{
    const auto source = read_text_file(
        std::filesystem::path(DRAXUL_PROJECT_ROOT)
        / "libs" / "draxul-nanovg" / "backend" / "src" / "nanovg_mtl.mm");
    REQUIRE_FALSE(source.empty());

    const auto create = source.find("NVGcontext* nvgCreateMtl");
    REQUIRE(create != std::string::npos);
    const auto delete_api = source.find("void nvgDeleteMtl", create);
    REQUIRE(delete_api != std::string::npos);
    const auto create_body = source.substr(create, delete_api - create);

    CHECK(create_body.find("MtlNVGCreation creation") != std::string::npos);
    CHECK(create_body.find("params.renderDelete = mtlnvg__renderDelete") != std::string::npos);
    CHECK(create_body.find("if (!ctx)") != std::string::npos);
    CHECK(create_body.find("creation.backend.release()") != std::string::npos);
    CHECK(source.find("mtl->creation->backend.release()") != std::string::npos);
}
