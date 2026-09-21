#include "support/test_support.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

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
    const auto present = source.find("vkQueuePresentKHR", capture_readback);
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

    const auto submit = source.find("vkQueueSubmit(ctx_.graphics_queue(), 1, &submit, in_flight_fences_[current_frame_])");
    const auto associate = source.find("images_in_flight_[current_image_] = in_flight_fences_[current_frame_]", submit);
    REQUIRE(submit != std::string::npos);
    REQUIRE(associate != std::string::npos);
    CHECK(submit < associate);
}

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
