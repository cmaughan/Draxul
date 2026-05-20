#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace draxul::markdown
{

struct SourceSpan
{
    size_t byte_offset = 0;
    size_t byte_length = 0;
    int start_line = 0;
    int end_line = 0;
};

enum class InlineKind
{
    Text,
    Emphasis,
    Strong,
    Code,
    Link,
    Image,
    SoftBreak,
    LineBreak,
};

struct Inline
{
    InlineKind kind = InlineKind::Text;
    std::string text;
    std::string destination;
    std::vector<Inline> children;
    SourceSpan source;
};

enum class BlockKind
{
    Document,
    FrontMatter,
    Paragraph,
    Heading,
    ThematicBreak,
    BlockQuote,
    Callout,
    List,
    ListItem,
    TaskItem,
    CodeBlock,
    Table,
    TableRow,
    TableCell,
    HtmlBlock,
};

enum class TableCellAlignment
{
    Default,
    Left,
    Center,
    Right,
};

struct FrontMatterEntry
{
    std::string key;
    std::string value;
};

struct Block
{
    BlockKind kind = BlockKind::Paragraph;
    int heading_level = 0;
    bool ordered = false;
    bool checked = false;
    bool table_header = false;
    int table_column_count = 0;
    TableCellAlignment table_alignment = TableCellAlignment::Default;
    std::string language;
    std::string literal;
    std::string callout_type;
    std::string callout_title;
    std::vector<FrontMatterEntry> front_matter;
    std::vector<Inline> inlines;
    std::vector<Block> children;
    SourceSpan source;
};

struct Document
{
    std::filesystem::path source_path;
    std::string source_text;
    std::vector<Block> blocks;
    std::vector<std::string> warnings;
};

} // namespace draxul::markdown
