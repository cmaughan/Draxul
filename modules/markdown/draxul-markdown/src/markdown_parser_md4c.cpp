#include <draxul/markdown/markdown_parser.h>

#include <md4c.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace draxul::markdown
{
namespace
{
std::string attr_to_string(const MD_ATTRIBUTE& attr)
{
    return std::string(attr.text, attr.text + attr.size);
}

std::string lowercase(std::string value)
{
    std::ranges::transform(value, value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string trim(std::string_view value)
{
    auto begin = value.begin();
    auto end = value.end();
    while (begin != end && std::isspace(static_cast<unsigned char>(*begin)))
        ++begin;
    while (begin != end && std::isspace(static_cast<unsigned char>(*(end - 1))))
        --end;
    return std::string(begin, end);
}

bool is_allowed_callout_type(std::string_view type)
{
    static constexpr std::string_view allowed[] = {
        "warning",
        "note",
        "tip",
        "info",
        "todo",
        "question",
        "example",
    };
    return std::ranges::find(allowed, type) != std::end(allowed);
}

SourceSpan approximate_span(size_t offset, size_t length)
{
    SourceSpan span;
    span.byte_offset = offset;
    span.byte_length = std::max<size_t>(length, 1);
    span.start_line = 1;
    span.end_line = 1;
    return span;
}

Inline make_text_inline(std::string text, SourceSpan source = {})
{
    Inline inline_node;
    inline_node.kind = InlineKind::Text;
    inline_node.text = std::move(text);
    inline_node.source = source;
    return inline_node;
}

void append_plain_text(std::vector<Inline>& destination, std::string_view text)
{
    if (text.empty())
        return;

    if (!destination.empty() && destination.back().kind == InlineKind::Text
        && destination.back().children.empty())
    {
        destination.back().text.append(text);
        destination.back().source.byte_length += text.size();
        return;
    }

    destination.push_back(make_text_inline(
        std::string(text),
        approximate_span(0, text.size())));
}

void append_text_with_wikilinks(std::vector<Inline>& destination, std::string_view text)
{
    size_t cursor = 0;
    while (cursor < text.size())
    {
        const size_t open = text.find("[[", cursor);
        if (open == std::string_view::npos)
        {
            append_plain_text(destination, text.substr(cursor));
            return;
        }

        append_plain_text(destination, text.substr(cursor, open - cursor));
        const size_t close = text.find("]]", open + 2);
        if (close == std::string_view::npos)
        {
            append_plain_text(destination, text.substr(open));
            return;
        }

        const std::string_view body = text.substr(open + 2, close - open - 2);
        const size_t pipe = body.find('|');
        const std::string target = pipe == std::string_view::npos
            ? std::string(body)
            : std::string(body.substr(0, pipe));
        const std::string label = pipe == std::string_view::npos
            ? target
            : std::string(body.substr(pipe + 1));

        Inline link;
        link.kind = InlineKind::Link;
        link.destination = target;
        link.children.push_back(make_text_inline(label, approximate_span(open, body.size())));
        link.source = approximate_span(open, close + 2 - open);
        destination.push_back(std::move(link));

        cursor = close + 2;
    }
}

std::string collect_inline_text(const std::vector<Inline>& inlines)
{
    std::string text;
    for (const auto& inline_node : inlines)
    {
        // Break nodes already carry their own newline as text, so emit exactly one
        // here -- emitting both is what used to leave a blank line between every
        // authored line. Callout headers split this reconstruction on the first
        // newline, so both break kinds must stay newlines.
        if (inline_node.kind == InlineKind::SoftBreak || inline_node.kind == InlineKind::LineBreak)
        {
            text += '\n';
            continue;
        }

        if (!inline_node.text.empty())
            text += inline_node.text;
        if (!inline_node.children.empty())
            text += collect_inline_text(inline_node.children);
    }
    return text;
}

struct CalloutHeader
{
    bool matched = false;
    std::string type;
    std::string title;
    std::string body;
};

CalloutHeader parse_callout_header(std::string_view text)
{
    if (!text.starts_with("[!"))
        return {};

    const size_t close = text.find(']');
    if (close == std::string_view::npos || close <= 2)
        return {};

    auto type = lowercase(std::string(text.substr(2, close - 2)));
    if (!is_allowed_callout_type(type))
        return {};

    std::string_view rest = text.substr(close + 1);
    while (!rest.empty() && (rest.front() == ' ' || rest.front() == '\t'))
        rest.remove_prefix(1);

    const size_t line_end = rest.find('\n');
    CalloutHeader header;
    header.matched = true;
    header.type = std::move(type);
    header.title = trim(line_end == std::string_view::npos ? rest : rest.substr(0, line_end));
    if (line_end != std::string_view::npos)
        header.body = trim(rest.substr(line_end + 1));
    return header;
}

Block paragraph_from_text(std::string text)
{
    Block paragraph;
    paragraph.kind = BlockKind::Paragraph;
    paragraph.source = approximate_span(0, text.size());
    append_text_with_wikilinks(paragraph.inlines, text);
    return paragraph;
}

void transform_callouts(std::vector<Block>& blocks)
{
    for (auto& block : blocks)
    {
        transform_callouts(block.children);
        if (block.kind != BlockKind::BlockQuote || block.children.empty())
            continue;

        const auto text = collect_inline_text(block.children.front().inlines);
        auto header = parse_callout_header(text);
        if (!header.matched)
            continue;

        block.kind = BlockKind::Callout;
        block.callout_type = std::move(header.type);
        block.callout_title = std::move(header.title);
        block.children.clear();
        if (!header.body.empty())
            block.children.push_back(paragraph_from_text(std::move(header.body)));
    }
}

BlockKind block_kind_from_md(MD_BLOCKTYPE type)
{
    switch (type)
    {
    case MD_BLOCK_QUOTE:
        return BlockKind::BlockQuote;
    case MD_BLOCK_UL:
    case MD_BLOCK_OL:
        return BlockKind::List;
    case MD_BLOCK_LI:
        return BlockKind::ListItem;
    case MD_BLOCK_HR:
        return BlockKind::ThematicBreak;
    case MD_BLOCK_H:
        return BlockKind::Heading;
    case MD_BLOCK_CODE:
        return BlockKind::CodeBlock;
    case MD_BLOCK_HTML:
        return BlockKind::HtmlBlock;
    case MD_BLOCK_P:
        return BlockKind::Paragraph;
    case MD_BLOCK_TABLE:
        return BlockKind::Table;
    case MD_BLOCK_TR:
        return BlockKind::TableRow;
    case MD_BLOCK_TH:
    case MD_BLOCK_TD:
        return BlockKind::TableCell;
    case MD_BLOCK_DOC:
    case MD_BLOCK_THEAD:
    case MD_BLOCK_TBODY:
        return BlockKind::Document;
    }
    return BlockKind::Paragraph;
}

InlineKind inline_kind_from_md(MD_SPANTYPE type)
{
    switch (type)
    {
    case MD_SPAN_EM:
        return InlineKind::Emphasis;
    case MD_SPAN_STRONG:
        return InlineKind::Strong;
    case MD_SPAN_A:
    case MD_SPAN_WIKILINK:
        return InlineKind::Link;
    case MD_SPAN_IMG:
        return InlineKind::Image;
    case MD_SPAN_CODE:
        return InlineKind::Code;
    case MD_SPAN_DEL:
    case MD_SPAN_LATEXMATH:
    case MD_SPAN_LATEXMATH_DISPLAY:
    case MD_SPAN_U:
        return InlineKind::Text;
    }
    return InlineKind::Text;
}

TableCellAlignment table_alignment_from_md(MD_ALIGN align)
{
    switch (align)
    {
    case MD_ALIGN_LEFT:
        return TableCellAlignment::Left;
    case MD_ALIGN_CENTER:
        return TableCellAlignment::Center;
    case MD_ALIGN_RIGHT:
        return TableCellAlignment::Right;
    case MD_ALIGN_DEFAULT:
    default:
        return TableCellAlignment::Default;
    }
}

struct ParserState
{
    Document document;
    ParseOptions options;
    size_t base_offset = 0;
    int table_head_depth = 0;
    std::vector<Block*> block_stack;
    std::vector<Inline*> inline_stack;

    std::vector<Inline>* inline_destination()
    {
        if (!inline_stack.empty())
            return &inline_stack.back()->children;
        if (!block_stack.empty())
            return &block_stack.back()->inlines;
        return nullptr;
    }

    Block& append_block(Block block)
    {
        if (block_stack.empty())
        {
            document.blocks.push_back(std::move(block));
            return document.blocks.back();
        }

        block_stack.back()->children.push_back(std::move(block));
        return block_stack.back()->children.back();
    }

    Inline& append_inline(Inline inline_node)
    {
        auto* destination = inline_destination();
        destination->push_back(std::move(inline_node));
        return destination->back();
    }
};

int enter_block_callback(MD_BLOCKTYPE type, void* detail, void* userdata)
{
    auto& state = *static_cast<ParserState*>(userdata);
    if (type == MD_BLOCK_THEAD)
    {
        ++state.table_head_depth;
        return 0;
    }
    if (type == MD_BLOCK_DOC || type == MD_BLOCK_TBODY)
        return 0;

    Block block;
    block.kind = block_kind_from_md(type);
    block.source = approximate_span(state.base_offset, 1);

    if (type == MD_BLOCK_TABLE)
    {
        const auto* table = static_cast<const MD_BLOCK_TABLE_DETAIL*>(detail);
        block.table_column_count = table != nullptr ? static_cast<int>(table->col_count) : 0;
    }
    else if (type == MD_BLOCK_TR)
    {
        block.table_header = state.table_head_depth > 0;
    }
    else if (type == MD_BLOCK_TH || type == MD_BLOCK_TD)
    {
        const auto* cell = static_cast<const MD_BLOCK_TD_DETAIL*>(detail);
        block.table_header = type == MD_BLOCK_TH || state.table_head_depth > 0;
        block.table_alignment = cell != nullptr ? table_alignment_from_md(cell->align) : TableCellAlignment::Default;
    }
    else if (type == MD_BLOCK_H)
    {
        const auto* heading = static_cast<const MD_BLOCK_H_DETAIL*>(detail);
        block.heading_level = static_cast<int>(heading->level);
    }
    else if (type == MD_BLOCK_OL)
    {
        block.ordered = true;
    }
    else if (type == MD_BLOCK_LI)
    {
        const auto* item = static_cast<const MD_BLOCK_LI_DETAIL*>(detail);
        if (item->is_task)
        {
            block.kind = BlockKind::TaskItem;
            block.checked = item->task_mark == 'x' || item->task_mark == 'X';
        }
    }
    else if (type == MD_BLOCK_CODE)
    {
        const auto* code = static_cast<const MD_BLOCK_CODE_DETAIL*>(detail);
        block.language = attr_to_string(code->lang);
    }

    Block& inserted = state.append_block(std::move(block));
    state.block_stack.push_back(&inserted);
    return 0;
}

int leave_block_callback(MD_BLOCKTYPE type, void*, void* userdata)
{
    auto& state = *static_cast<ParserState*>(userdata);
    if (type == MD_BLOCK_THEAD)
    {
        state.table_head_depth = std::max(0, state.table_head_depth - 1);
        return 0;
    }
    if (type == MD_BLOCK_DOC || type == MD_BLOCK_TBODY)
        return 0;

    if (!state.block_stack.empty())
        state.block_stack.pop_back();
    return 0;
}

int enter_span_callback(MD_SPANTYPE type, void* detail, void* userdata)
{
    auto& state = *static_cast<ParserState*>(userdata);
    if (state.inline_destination() == nullptr)
        return 0;

    Inline inline_node;
    inline_node.kind = inline_kind_from_md(type);
    inline_node.source = approximate_span(state.base_offset, 1);

    if (type == MD_SPAN_A)
    {
        const auto* link = static_cast<const MD_SPAN_A_DETAIL*>(detail);
        inline_node.destination = attr_to_string(link->href);
    }
    else if (type == MD_SPAN_WIKILINK)
    {
        const auto* link = static_cast<const MD_SPAN_WIKILINK_DETAIL*>(detail);
        inline_node.destination = attr_to_string(link->target);
    }

    Inline& inserted = state.append_inline(std::move(inline_node));
    state.inline_stack.push_back(&inserted);
    return 0;
}

int leave_span_callback(MD_SPANTYPE, void*, void* userdata)
{
    auto& state = *static_cast<ParserState*>(userdata);
    if (!state.inline_stack.empty())
        state.inline_stack.pop_back();
    return 0;
}

int text_callback(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size, void* userdata)
{
    auto& state = *static_cast<ParserState*>(userdata);
    if (state.block_stack.empty())
        return 0;

    auto& current_block = *state.block_stack.back();
    const std::string_view value(text, size);
    if (current_block.kind == BlockKind::CodeBlock || current_block.kind == BlockKind::HtmlBlock)
    {
        current_block.literal.append(value);
        current_block.source.byte_length += value.size();
        return 0;
    }

    if (!state.inline_stack.empty() && state.inline_stack.back()->kind == InlineKind::Code)
    {
        state.inline_stack.back()->text.append(value);
        state.inline_stack.back()->source.byte_length += value.size();
        return 0;
    }

    auto* destination = state.inline_destination();
    if (destination == nullptr)
        return 0;

    if (type == MD_TEXT_BR || type == MD_TEXT_SOFTBR)
    {
        Inline break_node;
        break_node.kind = type == MD_TEXT_BR ? InlineKind::LineBreak : InlineKind::SoftBreak;
        break_node.text = "\n";
        break_node.source = approximate_span(state.base_offset, 1);
        destination->push_back(std::move(break_node));
        return 0;
    }

    if (state.options.enable_wikilinks)
        append_text_with_wikilinks(*destination, value);
    else
        append_plain_text(*destination, value);
    return 0;
}

unsigned flags_from_options(const ParseOptions& options)
{
    unsigned flags = 0;
    if (options.enable_tables)
        flags |= MD_FLAG_TABLES;
    if (options.enable_task_lists)
        flags |= MD_FLAG_TASKLISTS;
    if (options.enable_strikethrough)
        flags |= MD_FLAG_STRIKETHROUGH;
    if (options.enable_wikilinks)
        flags |= MD_FLAG_WIKILINKS;
    return flags;
}
} // namespace

ParseResult parse_markdown(
    std::filesystem::path source_path,
    std::string source,
    ParseOptions options)
{
    ParserState state;
    state.options = options;
    state.document.source_path = std::move(source_path);
    state.document.source_text = std::move(source);

    const auto front_matter = detail::extract_front_matter(state.document.source_text);
    if (front_matter.found)
    {
        state.document.blocks.push_back(front_matter.block);
        state.base_offset = front_matter.content_offset;
    }

    const auto parse_source = std::string_view(state.document.source_text).substr(state.base_offset);
    MD_PARSER parser{};
    parser.abi_version = 0;
    parser.flags = flags_from_options(options);
    parser.enter_block = enter_block_callback;
    parser.leave_block = leave_block_callback;
    parser.enter_span = enter_span_callback;
    parser.leave_span = leave_span_callback;
    parser.text = text_callback;

    const int parse_status = md_parse(
        parse_source.data(),
        static_cast<MD_SIZE>(parse_source.size()),
        &parser,
        &state);

    ParseResult result;
    result.document = std::move(state.document);
    if (parse_status != 0)
    {
        result.ok = false;
        result.error = "MD4C parser failed";
        return result;
    }

    if (options.enable_callouts)
        transform_callouts(result.document.blocks);

    return result;
}

} // namespace draxul::markdown
