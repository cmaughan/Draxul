#include "support/test_support.h"

#include <catch2/catch_test_macros.hpp>
#include <draxul/text_atlas_builder.h>
#include <draxul/text_service.h>

#include <filesystem>

using namespace draxul;

namespace
{

TextService make_text_service()
{
    TextService service;
    tests::init_text_service(service, 11.0f);
    return service;
}

int first_covered_column(const TextAtlas& atlas, const TextAtlasEntry& entry)
{
    const int x0 = static_cast<int>(entry.uv_rect.x * atlas.image.width + 0.5f);
    const int y0 = static_cast<int>(entry.uv_rect.y * atlas.image.height + 0.5f);
    for (int x = 0; x < entry.pixel_size.x; ++x)
    {
        for (int y = 0; y < entry.pixel_size.y; ++y)
        {
            const size_t alpha = static_cast<size_t>(
                ((y0 + y) * atlas.image.width + x0 + x) * 4 + 3);
            if (atlas.image.rgba[alpha] != 0)
                return x;
        }
    }
    return entry.pixel_size.x;
}

} // namespace

TEST_CASE("text atlas builder packs deterministic keyed labels", "[font][text-atlas]")
{
    TextService service = make_text_service();
    const std::vector<TextAtlasRequest> requests = {
        { .key = "south", .text = "South", .target_pixel_size = { 80, 24 } },
        { .key = "north", .text = "North", .target_pixel_size = { 80, 24 } },
    };
    const TextAtlas first = build_text_atlas(service, requests, 7);
    const TextAtlas second = build_text_atlas(service, requests, 7);

    REQUIRE(first.image.valid());
    CHECK(first.image.revision == 7);
    CHECK(first.entries.size() == 2);
    CHECK(first.entries.at("north").pixel_size == glm::ivec2(80, 24));
    CHECK(first.entries.at("north").ink_pixel_size.x > 0);
    CHECK(first.image.rgba == second.image.rgba);
    CHECK(first.entries.at("north").uv_rect == second.entries.at("north").uv_rect);
}

TEST_CASE("text atlas builder handles UTF-8 labels without splitting bytes", "[font][text-atlas]")
{
    TextService service = make_text_service();
    const std::vector<TextAtlasRequest> requests = {
        { .key = "accented", .text = "Androm\xC3\xA8"
                                     "de",
            .target_pixel_size = { 128, 24 } },
    };
    const TextAtlas atlas = build_text_atlas(service, requests, 8);

    REQUIRE(atlas.image.valid());
    REQUIRE(atlas.entries.contains("accented"));
    CHECK(atlas.entries.at("accented").ink_pixel_size.x > 0);
}

TEST_CASE("text atlas builder filters empty and duplicate keys", "[font][text-atlas]")
{
    TextService service = make_text_service();
    const std::vector<TextAtlasRequest> requests = {
        { .key = "", .text = "ignored", .target_pixel_size = { 64, 20 } },
        { .key = "same", .text = "first", .target_pixel_size = { 64, 20 } },
        { .key = "same", .text = "second", .target_pixel_size = { 64, 20 } },
    };
    const TextAtlas atlas = build_text_atlas(service, requests, 9);

    CHECK(atlas.entries.size() == 1);
    CHECK(atlas.entries.contains("same"));
}

TEST_CASE("text atlas builder honors alignment and rejects oversized bitmaps", "[font][text-atlas]")
{
    TextService service = make_text_service();
    TextAtlasRequest start{ .key = "start", .text = "A", .target_pixel_size = { 96, 28 } };
    start.horizontal_align = TextAtlasHorizontalAlign::Start;
    TextAtlasRequest centered{ .key = "center", .text = "A", .target_pixel_size = { 96, 28 } };
    centered.horizontal_align = TextAtlasHorizontalAlign::Center;
    const std::vector requests = {
        start,
        centered,
        TextAtlasRequest{ .key = "oversized", .text = "skip", .target_pixel_size = { 9000, 32 } },
    };
    const TextAtlas atlas = build_text_atlas(service, requests, 10);

    REQUIRE(atlas.image.valid());
    REQUIRE(atlas.entries.contains("start"));
    REQUIRE(atlas.entries.contains("center"));
    CHECK_FALSE(atlas.entries.contains("oversized"));
    CHECK(first_covered_column(atlas, atlas.entries.at("center"))
        > first_covered_column(atlas, atlas.entries.at("start")));
}
