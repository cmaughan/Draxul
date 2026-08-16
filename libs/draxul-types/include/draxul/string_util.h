#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace draxul
{

// Strips leading/trailing whitespace (std::isspace) without allocating; the
// returned view references `value`'s buffer.
inline std::string_view trim_view(std::string_view value)
{
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
        value.remove_prefix(1);
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
        value.remove_suffix(1);
    return value;
}

inline std::string trim(std::string_view value)
{
    return std::string(trim_view(value));
}

inline std::string ascii_lower(std::string_view value)
{
    std::string lowered(value);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return lowered;
}

// Renders arbitrary (possibly binary) text as a quoted, escaped, ASCII-only
// string suitable for log lines: printable ASCII passes through, common
// control characters use C escapes, and everything else becomes \xNN.
inline std::string describe_text_for_log(std::string_view text)
{
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(text.size() * 4 + 2);
    out.push_back('"');
    for (const unsigned char ch : text)
    {
        switch (ch)
        {
        case '\\':
            out += "\\\\";
            break;
        case '"':
            out += "\\\"";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (ch >= 0x20 && ch <= 0x7E)
            {
                out.push_back(static_cast<char>(ch));
            }
            else
            {
                out += "\\x";
                out.push_back(kHex[(ch >> 4) & 0xF]);
                out.push_back(kHex[ch & 0xF]);
            }
            break;
        }
    }
    out.push_back('"');
    return out;
}

} // namespace draxul
