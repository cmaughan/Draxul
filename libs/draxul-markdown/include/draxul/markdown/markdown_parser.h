#pragma once

#include <draxul/markdown/markdown_document.h>

#include <filesystem>
#include <string>
#include <string_view>

namespace draxul::markdown
{

struct ParseOptions
{
    bool enable_tables = true;
    bool enable_task_lists = true;
    bool enable_strikethrough = true;
    bool enable_wikilinks = true;
    bool enable_callouts = true;
};

struct ParseResult
{
    Document document;
    bool ok = true;
    std::string error;
};

ParseResult parse_markdown(
    std::filesystem::path source_path,
    std::string source,
    ParseOptions options = {});

namespace detail
{
struct FrontMatterParse
{
    bool found = false;
    Block block;
    size_t content_offset = 0;
};

FrontMatterParse extract_front_matter(std::string_view source);
} // namespace detail

} // namespace draxul::markdown
