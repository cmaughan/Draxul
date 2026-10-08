#pragma once

#include <draxul/markdown/markdown_document.h>
#include <draxul/markdown/markdown_theme.h>
#include <draxul/rich_text_service.h>

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <utility>

namespace draxul::markdown
{

struct TextRun
{
    std::string text;
    StyleId style;
    float x = 0.0f;
    float baseline = 0.0f;
    SourceSpan source;
    // Overrides the style foreground; used by markers (task-list checkboxes)
    // that are drawn as glyphs but tinted with the theme accent.
    std::optional<draxul::Color> color;
    std::string link_destination;
    float width = 0.0f;
};

struct Decoration
{
    enum class Kind
    {
        Background,
        BorderLeft,
        Divider,
        Bullet,
        ScrollbarThumb,
        TableCellBackground,
        TableBorder,
    };

    Kind kind = Kind::Background;
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    draxul::Color color = draxul::Color(0.0f, 0.0f, 0.0f, 0.0f);
};

struct ImageRun
{
    std::string destination, alt, link_destination;
    float x=0, y=0, width=0, height=0;
};

struct LayoutRow
{
    float y = 0.0f;
    float height = 0.0f;
    float baseline = 0.0f;
    BlockKind source_kind = BlockKind::Paragraph;
    std::vector<TextRun> runs;
    std::vector<Decoration> decorations;
    SourceSpan source;
    std::vector<ImageRun> images;
};

struct LayoutDocument
{
    float content_width = 0.0f;
    float content_height = 0.0f;
    std::vector<LayoutRow> rows;
};

struct LayoutOptions
{
    float viewport_width = 1280.0f;
    float viewport_height = 720.0f;
    float pixel_scale = 1.0f;
    float margin_columns = 2.0f;
    // Hosts opt in to image layout and own resource loading/rendering.
    std::function<std::pair<float,float>(std::string_view)> image_size;
};

struct VisibleRowRange
{
    size_t first = 0;
    size_t count = 0;
};

bool is_web_link(std::string_view destination);
std::string_view markdown_link_at(const LayoutDocument& document,float x,float y);

using FontMetricsLookup
    = std::function<draxul::FontMetrics(const draxul::RichTextStyleKey&)>;

LayoutDocument layout_markdown_document(
    const Document& document,
    const MarkdownTheme& theme,
    const FontMetricsLookup& metrics_for,
    const LayoutOptions& options = {});

VisibleRowRange visible_rows(
    const LayoutDocument& document,
    float scroll_offset,
    float viewport_height);

} // namespace draxul::markdown
