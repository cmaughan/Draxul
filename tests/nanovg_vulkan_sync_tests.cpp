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
        / "libs" / "draxul-nanovg" / "src" / "nanovg_vk.cpp");
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
        / "libs" / "draxul-nanovg" / "src" / "nanovg_vk.cpp");
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

TEST_CASE("NanoVG dependency is immutable", "[nanovg][dependency]")
{
    const auto cmake = read_text_file(
        std::filesystem::path(DRAXUL_PROJECT_ROOT) / "cmake" / "FetchDependencies.cmake");
    REQUIRE_FALSE(cmake.empty());

    const auto declaration = cmake.find("FetchContent_Declare(\n    nanovg");
    REQUIRE(declaration != std::string::npos);
    const auto end = cmake.find(')', declaration);
    REQUIRE(end != std::string::npos);
    const auto block = cmake.substr(declaration, end - declaration);
    CHECK(block.find("GIT_TAG ce3bf745eb2d2dbc14a50bf2446783f691ac4353") != std::string::npos);
    CHECK(block.find("GIT_TAG master") == std::string::npos);
}
