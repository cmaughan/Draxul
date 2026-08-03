#include <catch2/catch_all.hpp>

#include <draxul/markdown/markdown_draw_list.h>
#include <draxul/markdown/markdown_theme.h>
#include <draxul/rich_text_service.h>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <string>
#include <type_traits>

using namespace draxul;
using namespace draxul::markdown;

namespace
{

std::filesystem::path repo_root()
{
    auto here = std::filesystem::path(__FILE__).parent_path();
    return here.parent_path();
}

TextServiceConfig rich_text_test_config()
{
    TextServiceConfig config;
    config.font_path = (repo_root() / "fonts" / "JetBrainsMonoNerdFont-Regular.ttf").string();
    return config;
}

RichTextService make_initialized_service(float base_point_size = 12.0f)
{
    auto config = rich_text_test_config();
    REQUIRE(std::filesystem::exists(config.font_path));

    RichTextService service;
    REQUIRE(service.initialize(config, base_point_size, 96.0f));
    return service;
}

LayoutRow text_row(float y, std::string text, StyleId style = {})
{
    LayoutRow row;
    row.y = y;
    row.height = 30.0f;
    row.baseline = y + 22.0f;
    row.runs.push_back(TextRun{
        .text = std::move(text),
        .style = style,
        .x = 8.0f,
        .baseline = row.baseline,
    });
    return row;
}

} // namespace

TEST_CASE("markdown draw-list instance structs have stable GPU-friendly layouts", "[markdown][drawlist]")
{
    STATIC_REQUIRE(std::is_standard_layout_v<MarkdownRectInstance>);
    STATIC_REQUIRE(std::is_standard_layout_v<MarkdownGlyphInstance>);
    REQUIRE(sizeof(MarkdownRectInstance) == 32);
    REQUIRE(sizeof(MarkdownGlyphInstance) == 64);
    REQUIRE(alignof(MarkdownRectInstance) == 16);
    REQUIRE(alignof(MarkdownGlyphInstance) == 16);

    REQUIRE(offsetof(MarkdownRectInstance, rect) == 0);
    REQUIRE(offsetof(MarkdownRectInstance, color) == 16);
    REQUIRE(offsetof(MarkdownGlyphInstance, rect) == 0);
    REQUIRE(offsetof(MarkdownGlyphInstance, uv) == 16);
    REQUIRE(offsetof(MarkdownGlyphInstance, color) == 32);
    REQUIRE(offsetof(MarkdownGlyphInstance, flags) == 48);
    REQUIRE(offsetof(MarkdownGlyphInstance, atlas_id) == 52);
    REQUIRE(offsetof(MarkdownGlyphInstance, atlas_generation) == 56);
    REQUIRE(offsetof(MarkdownGlyphInstance, _pad) == 60);
}

TEST_CASE("markdown draw-list reports visible work", "[markdown][drawlist]")
{
    MarkdownDrawList list;
    REQUIRE_FALSE(list.has_work());

    list.rects.push_back(MarkdownRectInstance{
        .rect = glm::vec4(0.0f, 0.0f, 10.0f, 10.0f),
        .color = Color(1.0f, 0.0f, 0.0f, 1.0f),
    });
    REQUIRE(list.has_work());
}

TEST_CASE("markdown draw-list emits rects for every decoration kind", "[markdown][drawlist]")
{
    auto service = make_initialized_service();
    MarkdownTheme theme;
    theme.document_background = Color(0.0f, 0.0f, 0.0f, 1.0f);

    LayoutDocument layout;
    LayoutRow row;
    row.y = 0.0f;
    row.height = 80.0f;
    row.decorations = {
        Decoration{
            .kind = Decoration::Kind::Background,
            .x = 1.0f,
            .y = 2.0f,
            .width = 30.0f,
            .height = 12.0f,
            .color = Color(0.2f, 0.3f, 0.4f, 1.0f),
        },
        Decoration{
            .kind = Decoration::Kind::BorderLeft,
            .x = 3.0f,
            .y = 18.0f,
            .width = 4.0f,
            .height = 40.0f,
            .color = Color(0.5f, 0.6f, 0.7f, 1.0f),
        },
        Decoration{
            .kind = Decoration::Kind::Divider,
            .x = 8.0f,
            .y = 24.0f,
            .width = 80.0f,
            .height = 2.0f,
            .color = Color(0.8f, 0.1f, 0.2f, 1.0f),
        },
        Decoration{
            .kind = Decoration::Kind::Bullet,
            .x = 12.0f,
            .y = 32.0f,
            .width = 2.0f,
            .height = 2.0f,
            .color = Color(0.1f, 0.8f, 0.3f, 1.0f),
        },
        Decoration{
            .kind = Decoration::Kind::ScrollbarThumb,
            .x = 94.0f,
            .y = 10.0f,
            .width = 4.0f,
            .height = 30.0f,
            .color = Color(0.7f, 0.7f, 0.8f, 1.0f),
        },
        Decoration{
            .kind = Decoration::Kind::TableCellBackground,
            .x = 4.0f,
            .y = 58.0f,
            .width = 40.0f,
            .height = 14.0f,
            .color = Color(0.2f, 0.2f, 0.3f, 1.0f),
        },
        Decoration{
            .kind = Decoration::Kind::TableBorder,
            .x = 44.0f,
            .y = 58.0f,
            .width = 1.0f,
            .height = 14.0f,
            .color = Color(0.6f, 0.7f, 0.8f, 1.0f),
        },
    };
    layout.rows.push_back(std::move(row));

    const auto list = build_markdown_draw_list(
        layout,
        theme,
        service,
        MarkdownDrawListOptions{ .viewport_width = 100, .viewport_height = 80, .pixel_scale = 1.0f });

    REQUIRE(list.viewport_width == 100);
    REQUIRE(list.viewport_height == 80);
    REQUIRE(list.clear_color == theme.document_background);
    REQUIRE(list.glyphs.empty());
    REQUIRE(list.glyph_batches.empty());
    REQUIRE(list.used_atlas_ids.empty());
    REQUIRE(list.rects.size() >= 7);
    REQUIRE(std::ranges::any_of(list.rects, [](const MarkdownRectInstance& rect) {
        return rect.rect == glm::vec4(1.0f, 2.0f, 30.0f, 12.0f);
    }));

    service.shutdown();
}

TEST_CASE("markdown draw-list batches body and heading glyphs by atlas snapshot", "[markdown][drawlist]")
{
    auto service = make_initialized_service();
    auto theme = default_markdown_theme(12.0f);

    LayoutDocument layout;
    layout.rows.push_back(text_row(0.0f, "Body text", StyleId{}));
    layout.rows.push_back(text_row(32.0f, "Heading", StyleId{ 1 }));

    const auto list = build_markdown_draw_list(
        layout,
        theme,
        service,
        MarkdownDrawListOptions{ .viewport_width = 320, .viewport_height = 120, .pixel_scale = 1.0f });

    REQUIRE(list.rects.empty());
    REQUIRE(list.glyphs.size() >= 2);
    REQUIRE(list.glyph_batches.size() >= 2);
    REQUIRE(list.used_atlas_ids.size() >= 2);
    for (const auto& glyph : list.glyphs)
    {
        REQUIRE(glyph.rect.z > 0.0f);
        REQUIRE(glyph.rect.w > 0.0f);
        REQUIRE(glyph.atlas_id != 0);
        REQUIRE(glyph.atlas_generation != 0);
    }
    for (const auto& batch : list.glyph_batches)
    {
        REQUIRE(batch.count > 0);
        REQUIRE(batch.first + batch.count <= list.glyphs.size());
    }

    service.shutdown();
}

TEST_CASE("markdown draw-list clips glyph emission to visible rows", "[markdown][drawlist]")
{
    auto service = make_initialized_service();
    auto theme = default_markdown_theme(12.0f);

    LayoutDocument layout;
    for (int index = 0; index < 20; ++index)
        layout.rows.push_back(text_row(static_cast<float>(index * 30), "A"));

    const auto list = build_markdown_draw_list(
        layout,
        theme,
        service,
        MarkdownDrawListOptions{
            .viewport_width = 160,
            .viewport_height = 90,
            .scroll_offset = 300.0f,
            .pixel_scale = 1.0f,
        });

    REQUIRE_FALSE(list.glyphs.empty());
    REQUIRE(list.glyphs.size() <= 4);
    for (const auto& glyph : list.glyphs)
    {
        REQUIRE(glyph.rect.y >= -40.0f);
        REQUIRE(glyph.rect.y <= 120.0f);
    }

    service.shutdown();
}

TEST_CASE("markdown draw-list emits bounded glyph chunks for long unbroken text", "[markdown][drawlist]")
{
    auto service = make_initialized_service();
    auto theme = default_markdown_theme(12.0f);

    LayoutDocument layout;
    layout.rows.push_back(text_row(0.0f, std::string(512, 'W')));

    const auto list = build_markdown_draw_list(
        layout,
        theme,
        service,
        MarkdownDrawListOptions{ .viewport_width = 4096, .viewport_height = 80, .pixel_scale = 1.0f });

    REQUIRE_FALSE(list.glyphs.empty());
    REQUIRE(std::ranges::all_of(list.glyphs, [](const MarkdownGlyphInstance& glyph) {
        return glyph.rect.z > 0.0f && glyph.rect.z < 512.0f;
    }));

    service.shutdown();
}

TEST_CASE("markdown draw-list does not start a glyph chunk with a combining mark", "[markdown][drawlist]")
{
    auto service = make_initialized_service();
    auto theme = default_markdown_theme(12.0f);

    LayoutDocument layout;
    layout.rows.push_back(text_row(0.0f, "AAAAAAAe\xCC\x81"));

    const auto list = build_markdown_draw_list(
        layout,
        theme,
        service,
        MarkdownDrawListOptions{ .viewport_width = 240, .viewport_height = 80, .pixel_scale = 1.0f });

    REQUIRE(list.glyphs.size() == 1);
    REQUIRE(list.glyphs.front().rect.z > 0.0f);
    REQUIRE(list.glyphs.front().rect.z < 512.0f);

    service.shutdown();
}

TEST_CASE("markdown draw-list does not split a regional indicator flag pair", "[markdown][drawlist]")
{
    auto service = make_initialized_service();
    auto theme = default_markdown_theme(12.0f);

    LayoutDocument layout;
    layout.rows.push_back(text_row(0.0f, "AAAAAAA\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8"));

    const auto list = build_markdown_draw_list(
        layout,
        theme,
        service,
        MarkdownDrawListOptions{ .viewport_width = 240, .viewport_height = 80, .pixel_scale = 1.0f });

    REQUIRE(list.glyphs.size() == 1);
    REQUIRE(list.glyphs.front().rect.z > 0.0f);
    REQUIRE(list.glyphs.front().rect.z < 512.0f);

    service.shutdown();
}

TEST_CASE("markdown draw-list collects dirty atlas upload pixels and clears dirtiness", "[markdown][drawlist]")
{
    auto service = make_initialized_service();
    auto theme = default_markdown_theme(12.0f);

    LayoutDocument layout;
    layout.rows.push_back(text_row(0.0f, "Upload"));

    const auto list = build_markdown_draw_list(
        layout,
        theme,
        service,
        MarkdownDrawListOptions{ .viewport_width = 240, .viewport_height = 80, .pixel_scale = 1.0f });
    REQUIRE_FALSE(list.used_atlas_ids.empty());

    const auto uploads = collect_markdown_atlas_uploads(service, list.used_atlas_ids, 77);

    REQUIRE_FALSE(uploads.empty());
    for (const auto& upload : uploads)
    {
        REQUIRE(upload.upload_revision == 77);
        REQUIRE(upload.atlas_id != 0);
        REQUIRE(upload.generation != 0);
        REQUIRE(upload.atlas_width > 0);
        REQUIRE(upload.atlas_height > 0);
        REQUIRE(upload.rect.size.x > 0);
        REQUIRE(upload.rect.size.y > 0);
        REQUIRE(upload.rgba.size()
            == static_cast<size_t>(upload.rect.size.x) * static_cast<size_t>(upload.rect.size.y) * 4u);
    }

    const auto clean_uploads = collect_markdown_atlas_uploads(service, list.used_atlas_ids, 78);
    REQUIRE(clean_uploads.empty());

    service.shutdown();
}
