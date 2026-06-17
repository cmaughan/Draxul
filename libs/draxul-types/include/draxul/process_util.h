#pragma once

#include <string>
#include <string_view>

namespace draxul
{

inline std::string quote_windows_arg(std::string_view arg)
{
    const bool needs_quotes = arg.empty()
        || arg.find_first_of(" \t\n\v\"") != std::string_view::npos;
    if (!needs_quotes)
        return std::string(arg);

    std::string quoted;
    quoted.push_back('"');
    size_t pending_backslashes = 0;
    for (char ch : arg)
    {
        if (ch == '\\')
        {
            ++pending_backslashes;
            continue;
        }

        if (ch == '"')
            quoted.append(pending_backslashes * 2 + 1, '\\');
        else
            quoted.append(pending_backslashes, '\\');
        pending_backslashes = 0;
        quoted.push_back(ch);
    }
    quoted.append(pending_backslashes * 2, '\\');
    quoted.push_back('"');
    return quoted;
}

} // namespace draxul
