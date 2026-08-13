#include <draxul/satview/satview_label_layout.h>

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <string>

using namespace draxul::satview;

namespace
{

SatViewLabelLayoutCandidate candidate(
    std::string key,
    glm::vec2 center,
    int rank,
    glm::vec2 size = { 80.0f, 20.0f })
{
    SatViewLabelLayoutCandidate result;
    result.key = std::move(key);
    result.ndc_center = center;
    result.rank = rank;
    result.instance.pixel_size_offset = glm::vec4(size, 0.0f, 0.0f);
    result.instance.direction_priority.w = static_cast<float>(rank);
    return result;
}

std::string read_text_file(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return {};
    return std::string(std::istreambuf_iterator<char>(in), {});
}

} // namespace

TEST_CASE("SatView label layout prioritizes rank and removes overlap", "[satview][labels]")
{
    const auto labels = layout_satview_labels({
                                                  candidate("rank-three", { 0.0f, 0.0f }, 3),
                                                  candidate("rank-one", { 0.0f, 0.0f }, 1),
                                                  candidate("separate", { 0.8f, 0.8f }, 2),
                                              },
        { 800, 600 });

    REQUIRE(labels.size() == 2);
    CHECK(labels[0].direction_priority.w == 1.0f);
    CHECK(labels[1].direction_priority.w == 2.0f);
}

TEST_CASE("SatView label layout is stable for equal ranks", "[satview][labels]")
{
    const auto labels = layout_satview_labels({
                                                  candidate("zeta", { 0.0f, 0.0f }, 1),
                                                  candidate("alpha", { 0.0f, 0.0f }, 1),
                                              },
        { 800, 600 });

    REQUIRE(labels.size() == 1);
    CHECK(labels[0].direction_priority.w == 1.0f);
}

TEST_CASE("SatView label layout rejects fully offscreen labels", "[satview][labels]")
{
    const auto labels = layout_satview_labels({
                                                  candidate("outside", { 3.0f, 0.0f }, 1),
                                                  candidate("inside", { 0.0f, 0.0f }, 1),
                                              },
        { 800, 600 });

    CHECK(labels.size() == 1);
}

TEST_CASE("SatView label layout reserves celestial body rectangles", "[satview][labels]")
{
    const std::array exclusions = {
        SatViewLabelLayoutExclusion{ { 0.0f, 0.0f }, { 120.0f, 120.0f } },
    };
    const auto labels = layout_satview_labels({
                                                  candidate("behind-body", { 0.0f, 0.0f }, 1),
                                                  candidate("clear", { 0.75f, 0.0f }, 2),
                                              },
        { 800, 600 }, exclusions);

    REQUIRE(labels.size() == 1);
    CHECK(labels[0].direction_priority.w == 2.0f);
}

TEST_CASE("SatView Metal labels convert top-left pixel offsets to Metal clip space",
    "[satview][labels][shader]")
{
    const auto source = read_text_file(
        std::filesystem::path(DRAXUL_PROJECT_ROOT)
        / "plugins" / "satview" / "shaders" / "satview_scene.metal");
    REQUIRE(!source.empty());

    CHECK(source.find("float2 clip_pixel_offset = float2(pixel_offset.x, -pixel_offset.y);")
        != std::string::npos);
    CHECK(source.find("projected.xy += clip_pixel_offset * (2.0f / viewport) * projected.w;")
        != std::string::npos);
}
