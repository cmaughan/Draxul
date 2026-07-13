#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{

std::string read_text_file(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return {};
    std::string text(std::istreambuf_iterator<char>(in), {});
    text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
    return text;
}

std::string source_block(
    const std::string& source,
    std::string_view begin_marker,
    std::string_view end_marker)
{
    const std::size_t begin = source.find(begin_marker);
    if (begin == std::string::npos)
        return {};
    const std::size_t end = source.find(end_marker, begin);
    if (end == std::string::npos)
        return source.substr(begin);
    return source.substr(begin, end - begin);
}

} // namespace

TEST_CASE("SatView rings use opaque depth-writing render state", "[satview][rings][shader]")
{
    const auto metal_source = read_text_file(
        std::filesystem::path(DRAXUL_PROJECT_ROOT)
        / "modules" / "satview" / "draxul-satview" / "src" / "satview_render.mm");
    REQUIRE(!metal_source.empty());

    const std::string metal_ring_pipeline = source_block(
        metal_source,
        "desc.vertexFunction = ring_vertex;",
        "ring_pipeline.reset(created);");
    REQUIRE_FALSE(metal_ring_pipeline.empty());
    CHECK(metal_ring_pipeline.find("blendingEnabled = NO") != std::string::npos);
    CHECK(metal_ring_pipeline.find("blendingEnabled = YES") == std::string::npos);
    CHECK(metal_source.find("[encoder setDepthStencilState:state_->depth_write_state.get()];\n"
                            "            for (const SatViewRingBand& band : ring_bands_)")
        != std::string::npos);

    const auto vulkan_source = read_text_file(
        std::filesystem::path(DRAXUL_PROJECT_ROOT)
        / "modules" / "satview" / "draxul-satview" / "src" / "satview_render_vk.cpp");
    REQUIRE(!vulkan_source.empty());

    const std::string vulkan_ring_pipeline = source_block(
        vulkan_source,
        "stages[0].module = ring_vert;",
        "VK_NULL_HANDLE, 1, &pipeline_ci, nullptr, &ring_pipeline);");
    REQUIRE_FALSE(vulkan_ring_pipeline.empty());
    CHECK(vulkan_ring_pipeline.find("blend_attachment.blendEnable = VK_TRUE") == std::string::npos);
    CHECK(vulkan_ring_pipeline.find("depth.depthWriteEnable = VK_FALSE") == std::string::npos);
}
