#pragma once

#include <draxul/markdown/markdown_layout.h>
#include <draxul/rich_text_service.h>
#include <draxul/types.h>

#include <cstdint>
#include <span>
#include <vector>

namespace draxul::markdown
{

struct alignas(16) MarkdownRectInstance
{
    glm::vec4 rect = {};
    glm::vec4 color = {};
};

struct alignas(16) MarkdownGlyphInstance
{
    glm::vec4 rect = {};
    glm::vec4 uv = {};
    glm::vec4 color = {};
    uint32_t flags = 0;
    uint32_t atlas_id = 0;
    uint32_t atlas_generation = 0;
    uint32_t _pad = 0;
};

static_assert(sizeof(MarkdownRectInstance) == 32);
static_assert(sizeof(MarkdownGlyphInstance) == 64);
static_assert(alignof(MarkdownRectInstance) == 16);
static_assert(alignof(MarkdownGlyphInstance) == 16);

struct MarkdownGlyphBatch
{
    uint32_t atlas_id = 0;
    uint32_t atlas_generation = 0;
    uint32_t first = 0;
    uint32_t count = 0;
};

struct MarkdownAtlasUpload
{
    uint32_t atlas_id = 0;
    uint32_t generation = 0;
    uint64_t upload_revision = 0;
    int atlas_width = 0;
    int atlas_height = 0;
    AtlasDirtyRect rect = {};
    std::vector<uint8_t> rgba;
    bool full_upload = false;
};

struct MarkdownDrawList
{
    std::vector<MarkdownRectInstance> rects;
    std::vector<MarkdownGlyphInstance> glyphs;
    std::vector<MarkdownGlyphBatch> glyph_batches;
    std::vector<uint32_t> used_atlas_ids;
    Color clear_color = Color(0.0f, 0.0f, 0.0f, 0.0f);
    int viewport_width = 0;
    int viewport_height = 0;

    bool has_work() const
    {
        return !rects.empty() || !glyphs.empty();
    }
};

struct MarkdownDrawListOptions
{
    int viewport_width = 0;
    int viewport_height = 0;
    float scroll_offset = 0.0f;
    float pixel_scale = 1.0f;
};

MarkdownDrawList build_markdown_draw_list(
    const LayoutDocument& document,
    const MarkdownTheme& theme,
    draxul::RichTextService& rich_text,
    const MarkdownDrawListOptions& options);

std::vector<MarkdownAtlasUpload> collect_markdown_atlas_uploads(
    draxul::RichTextService& rich_text,
    std::span<const uint32_t> used_atlas_ids,
    uint64_t upload_revision);

} // namespace draxul::markdown
