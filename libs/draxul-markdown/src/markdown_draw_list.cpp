#include <draxul/markdown/markdown_draw_list.h>

#include <draxul/unicode.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>
#include <string_view>

namespace draxul::markdown
{
namespace
{

constexpr int kMaxClusterCodepoints = 8;
constexpr int kMaxBuildAttempts = 3;
constexpr int kChannels = 4;

const MarkdownTextStyle& style_for_id(const MarkdownTheme& theme, StyleId style)
{
    switch (style.value)
    {
    case 1:
        return theme.heading1;
    case 2:
        return theme.heading2;
    case 3:
        return theme.heading3;
    case 4:
        return theme.heading4;
    case 5:
        return theme.heading5;
    case 6:
        return theme.heading6;
    case 7:
        return theme.code;
    default:
        return theme.body;
    }
}

void build_glyph_batches(MarkdownDrawList& list);

MarkdownDrawList empty_markdown_draw_list(const MarkdownTheme& theme, const MarkdownDrawListOptions& options)
{
    MarkdownDrawList list;
    list.viewport_width = std::max(0, options.viewport_width);
    list.viewport_height = std::max(0, options.viewport_height);
    list.clear_color = theme.document_background;
    return list;
}

void add_rect(MarkdownDrawList& list, float x, float y, float width, float height, const draxul::Color& color)
{
    if (width <= 0.0f || height <= 0.0f || color.a <= 0.0f)
        return;

    list.rects.push_back(MarkdownRectInstance{
        .rect = glm::vec4(x, y, width, height),
        .color = color,
    });
}

void add_bullet(MarkdownDrawList& list, const Decoration& decoration, float scroll_offset, float pixel_scale)
{
    const float size = std::max({ decoration.width, decoration.height, 3.0f * pixel_scale });
    add_rect(list, decoration.x, decoration.y - scroll_offset, size, size, decoration.color);
}

void add_checkbox(MarkdownDrawList& list, const Decoration& decoration, float scroll_offset, float pixel_scale)
{
    const float thickness = std::max(1.0f, pixel_scale);
    const float x = decoration.x;
    const float y = decoration.y - scroll_offset;
    const float width = std::max(decoration.width, 6.0f * pixel_scale);
    const float height = std::max(decoration.height, 6.0f * pixel_scale);

    add_rect(list, x, y, width, thickness, decoration.color);
    add_rect(list, x, y + height - thickness, width, thickness, decoration.color);
    add_rect(list, x, y, thickness, height, decoration.color);
    add_rect(list, x + width - thickness, y, thickness, height, decoration.color);

    if (!decoration.checked)
        return;

    add_rect(list, x + thickness * 2.0f, y + height * 0.5f, width * 0.28f, thickness, decoration.color);
    add_rect(list, x + width * 0.45f, y + height * 0.3f, thickness, height * 0.45f, decoration.color);
}

void add_decoration(MarkdownDrawList& list, const Decoration& decoration, float scroll_offset, float pixel_scale)
{
    switch (decoration.kind)
    {
    case Decoration::Kind::Background:
    case Decoration::Kind::BorderLeft:
    case Decoration::Kind::Divider:
    case Decoration::Kind::ScrollbarThumb:
    case Decoration::Kind::TableCellBackground:
    case Decoration::Kind::TableBorder:
        add_rect(
            list,
            decoration.x,
            decoration.y - scroll_offset,
            decoration.width,
            decoration.height,
            decoration.color);
        break;
    case Decoration::Kind::Bullet:
        add_bullet(list, decoration, scroll_offset, pixel_scale);
        break;
    case Decoration::Kind::Checkbox:
        add_checkbox(list, decoration, scroll_offset, pixel_scale);
        break;
    }
}

bool should_extend_chunk_after(uint32_t cp)
{
    return cp == 0x200D || draxul::is_width_ignorable(cp) || draxul::is_emoji_modifier(cp);
}

bool is_regional_indicator(uint32_t cp)
{
    return cp >= 0x1F1E6 && cp <= 0x1F1FF;
}

bool should_extend_chunk_before(std::string_view text, size_t offset, uint32_t previous)
{
    uint32_t next = 0;
    if (offset >= text.size() || !draxul::utf8_decode_next(text, offset, next))
        return false;

    return should_extend_chunk_after(next) || (is_regional_indicator(previous) && is_regional_indicator(next));
}

float add_text_cluster(
    MarkdownDrawList& list,
    std::string_view text,
    const MarkdownTextStyle& style,
    draxul::RichTextService& rich_text,
    float x,
    float baseline,
    float scroll_offset)
{
    if (text.empty())
        return 0.0f;

    const RichTextCluster cluster = rich_text.resolve_cluster(std::string(text), style.rich_text);
    const AtlasRegion& region = cluster.atlas;
    if (region.bitmap_size.x > 0 && region.bitmap_size.y > 0)
    {
        list.glyphs.push_back(MarkdownGlyphInstance{
            .rect = glm::vec4(
                x + static_cast<float>(region.bitmap_bearing.x),
                baseline - scroll_offset - static_cast<float>(region.bitmap_bearing.y),
                static_cast<float>(region.bitmap_size.x),
                static_cast<float>(region.bitmap_size.y)),
            .uv = region.uv,
            .color = style.foreground,
            .flags = region.is_color ? STYLE_FLAG_COLOR_GLYPH : 0u,
            .atlas_id = cluster.atlas_id,
            .atlas_generation = cluster.atlas_generation,
        });
    }

    return cluster.advance_px;
}

void add_text_run(
    MarkdownDrawList& list,
    const TextRun& run,
    const MarkdownTheme& theme,
    draxul::RichTextService& rich_text,
    float scroll_offset,
    int viewport_width)
{
    if (run.text.empty())
        return;

    const auto& style = style_for_id(theme, run.style);
    float pen_x = run.x;
    size_t chunk_start = 0;
    size_t offset = 0;
    int codepoints = 0;

    while (offset < run.text.size())
    {
        uint32_t cp = 0;
        if (!draxul::utf8_decode_next(run.text, offset, cp))
            break;

        ++codepoints;
        if ((codepoints < kMaxClusterCodepoints || should_extend_chunk_after(cp)
                || should_extend_chunk_before(run.text, offset, cp))
            && offset < run.text.size())
        {
            continue;
        }

        const std::string_view chunk(run.text.data() + chunk_start, offset - chunk_start);
        pen_x += add_text_cluster(list, chunk, style, rich_text, pen_x, run.baseline, scroll_offset);
        chunk_start = offset;
        codepoints = 0;

        if (pen_x > static_cast<float>(viewport_width))
            break;
    }
}

MarkdownDrawList build_markdown_draw_list_once(
    const LayoutDocument& document,
    const MarkdownTheme& theme,
    draxul::RichTextService& rich_text,
    const MarkdownDrawListOptions& options)
{
    MarkdownDrawList list = empty_markdown_draw_list(theme, options);

    if (list.viewport_width <= 0 || list.viewport_height <= 0)
        return list;

    const float scroll_offset = options.scroll_offset;
    const float pixel_scale = std::max(0.01f, options.pixel_scale);
    const VisibleRowRange range = visible_rows(document, scroll_offset, static_cast<float>(list.viewport_height));
    const size_t end = std::min(document.rows.size(), range.first + range.count);

    for (size_t index = range.first; index < end; ++index)
    {
        const LayoutRow& row = document.rows[index];
        for (const Decoration& decoration : row.decorations)
            add_decoration(list, decoration, scroll_offset, pixel_scale);
        for (const TextRun& run : row.runs)
            add_text_run(list, run, theme, rich_text, scroll_offset, list.viewport_width);
    }

    build_glyph_batches(list);
    return list;
}

void build_glyph_batches(MarkdownDrawList& list)
{
    std::vector<size_t> order(list.glyphs.size());
    std::iota(order.begin(), order.end(), 0u);
    std::stable_sort(order.begin(), order.end(), [&list](size_t lhs_index, size_t rhs_index) {
        const auto& lhs = list.glyphs[lhs_index];
        const auto& rhs = list.glyphs[rhs_index];
        if (lhs.atlas_id != rhs.atlas_id)
            return lhs.atlas_id < rhs.atlas_id;
        return lhs.atlas_generation < rhs.atlas_generation;
    });

    std::vector<MarkdownGlyphInstance> sorted_glyphs;
    sorted_glyphs.reserve(list.glyphs.size());
    for (size_t index : order)
        sorted_glyphs.push_back(list.glyphs[index]);
    list.glyphs = std::move(sorted_glyphs);

    list.glyph_batches.clear();
    list.used_atlas_ids.clear();
    if (list.glyphs.empty())
        return;

    uint32_t batch_first = 0;
    uint32_t batch_atlas_id = list.glyphs.front().atlas_id;
    uint32_t batch_generation = list.glyphs.front().atlas_generation;

    auto remember_atlas = [&list](uint32_t atlas_id) {
        if (std::ranges::find(list.used_atlas_ids, atlas_id) == list.used_atlas_ids.end())
            list.used_atlas_ids.push_back(atlas_id);
    };
    remember_atlas(batch_atlas_id);

    for (uint32_t index = 1; index < list.glyphs.size(); ++index)
    {
        const auto& glyph = list.glyphs[index];
        remember_atlas(glyph.atlas_id);
        if (glyph.atlas_id == batch_atlas_id && glyph.atlas_generation == batch_generation)
            continue;

        list.glyph_batches.push_back(MarkdownGlyphBatch{
            .atlas_id = batch_atlas_id,
            .atlas_generation = batch_generation,
            .first = batch_first,
            .count = index - batch_first,
        });
        batch_first = index;
        batch_atlas_id = glyph.atlas_id;
        batch_generation = glyph.atlas_generation;
    }

    list.glyph_batches.push_back(MarkdownGlyphBatch{
        .atlas_id = batch_atlas_id,
        .atlas_generation = batch_generation,
        .first = batch_first,
        .count = static_cast<uint32_t>(list.glyphs.size()) - batch_first,
    });
}

AtlasDirtyRect full_atlas_rect(const RichTextAtlasSnapshot& snapshot)
{
    return AtlasDirtyRect{
        .pos = glm::ivec2(0, 0),
        .size = glm::ivec2(snapshot.width, snapshot.height),
    };
}

AtlasDirtyRect clamped_dirty_rect(const RichTextAtlasSnapshot& snapshot)
{
    AtlasDirtyRect rect = snapshot.reset_pending ? full_atlas_rect(snapshot) : snapshot.dirty_rect;
    rect.pos.x = std::clamp(rect.pos.x, 0, snapshot.width);
    rect.pos.y = std::clamp(rect.pos.y, 0, snapshot.height);
    rect.size.x = std::clamp(rect.size.x, 0, snapshot.width - rect.pos.x);
    rect.size.y = std::clamp(rect.size.y, 0, snapshot.height - rect.pos.y);
    return rect;
}

} // namespace

MarkdownDrawList build_markdown_draw_list(
    const LayoutDocument& document,
    const MarkdownTheme& theme,
    draxul::RichTextService& rich_text,
    const MarkdownDrawListOptions& options)
{
    for (int attempt = 0; attempt < kMaxBuildAttempts; ++attempt)
    {
        MarkdownDrawList list = build_markdown_draw_list_once(document, theme, rich_text, options);
        if (!rich_text.consume_any_atlas_reset())
            return list;
    }

    return empty_markdown_draw_list(theme, options);
}

std::vector<MarkdownAtlasUpload> collect_markdown_atlas_uploads(
    draxul::RichTextService& rich_text,
    std::span<const uint32_t> used_atlas_ids,
    uint64_t upload_revision)
{
    std::vector<MarkdownAtlasUpload> uploads;
    for (uint32_t atlas_id : used_atlas_ids)
    {
        const auto snapshot = rich_text.atlas_snapshot(atlas_id);
        if (!snapshot.has_value() || snapshot->data == nullptr || snapshot->width <= 0 || snapshot->height <= 0)
            continue;
        if (!snapshot->dirty && !snapshot->reset_pending)
            continue;

        const AtlasDirtyRect rect = clamped_dirty_rect(*snapshot);
        if (rect.size.x <= 0 || rect.size.y <= 0)
            continue;

        MarkdownAtlasUpload upload;
        upload.atlas_id = snapshot->atlas_id;
        upload.generation = snapshot->generation;
        upload.upload_revision = upload_revision;
        upload.atlas_width = snapshot->width;
        upload.atlas_height = snapshot->height;
        upload.rect = rect;
        upload.full_upload = snapshot->reset_pending;

        const size_t row_bytes = static_cast<size_t>(rect.size.x) * static_cast<size_t>(kChannels);
        upload.rgba.resize(row_bytes * static_cast<size_t>(rect.size.y));
        for (int row = 0; row < rect.size.y; ++row)
        {
            const size_t src_offset =
                (static_cast<size_t>(rect.pos.y + row) * static_cast<size_t>(snapshot->width)
                    + static_cast<size_t>(rect.pos.x))
                * static_cast<size_t>(kChannels);
            const size_t dst_offset = static_cast<size_t>(row) * row_bytes;
            std::memcpy(upload.rgba.data() + dst_offset, snapshot->data + src_offset, row_bytes);
        }

        rich_text.clear_atlas_dirty(atlas_id);
        uploads.push_back(std::move(upload));
    }

    return uploads;
}

} // namespace draxul::markdown
