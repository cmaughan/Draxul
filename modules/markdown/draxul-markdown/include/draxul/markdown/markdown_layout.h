#pragma once

#include <draxul/markdown/markdown_document.h>
#include <draxul/markdown/markdown_theme.h>
#include <draxul/rich_text_service.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace draxul::markdown
{

struct StyleId
{
    uint16_t value = 0;
};

struct TextRun
{
    std::string text;
    StyleId style;
    float x = 0.0f;
    float baseline = 0.0f;
    SourceSpan source;
};

struct Decoration
{
    enum class Kind
    {
        Background,
        BorderLeft,
        Divider,
        Checkbox,
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
    bool checked = false;
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
};

struct VisibleRowRange
{
    size_t first = 0;
    size_t count = 0;
};

LayoutDocument layout_markdown_document(
    const Document& document,
    const MarkdownTheme& theme,
    draxul::RichTextService& rich_text,
    const LayoutOptions& options = {});

VisibleRowRange visible_rows(
    const LayoutDocument& document,
    float scroll_offset,
    float viewport_height);

} // namespace draxul::markdown
