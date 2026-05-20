#include <draxul/markdown/markdown_parser.h>

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>

namespace draxul::markdown
{
namespace detail
{
namespace
{
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

bool is_front_matter_delimiter(std::string_view line)
{
    if (!line.empty() && line.back() == '\r')
        line.remove_suffix(1);
    return line == "---" || line == "...";
}
} // namespace

FrontMatterParse extract_front_matter(std::string_view source)
{
    FrontMatterParse parsed;
    if (!source.starts_with("---"))
        return parsed;

    const size_t first_line_end = source.find('\n');
    if (first_line_end == std::string_view::npos)
        return parsed;
    std::string_view first_line = source.substr(0, first_line_end);
    if (!is_front_matter_delimiter(first_line))
        return parsed;

    size_t line_start = first_line_end + 1;
    while (line_start <= source.size())
    {
        const size_t line_end = source.find('\n', line_start);
        const size_t line_size = line_end == std::string_view::npos
            ? source.size() - line_start
            : line_end - line_start;
        const std::string_view line = source.substr(line_start, line_size);

        if (is_front_matter_delimiter(line))
        {
            parsed.found = true;
            parsed.content_offset = line_end == std::string_view::npos ? source.size() : line_end + 1;
            parsed.block.kind = BlockKind::FrontMatter;
            parsed.block.source.byte_offset = 0;
            parsed.block.source.byte_length = parsed.content_offset;
            parsed.block.source.start_line = 1;
            parsed.block.source.end_line = 1;
            return parsed;
        }

        const size_t colon = line.find(':');
        if (colon != std::string_view::npos)
        {
            FrontMatterEntry entry;
            entry.key = trim(line.substr(0, colon));
            entry.value = trim(line.substr(colon + 1));
            if (!entry.key.empty())
                parsed.block.front_matter.push_back(std::move(entry));
        }

        if (line_end == std::string_view::npos)
            break;
        line_start = line_end + 1;
    }

    return {};
}

} // namespace detail
} // namespace draxul::markdown
