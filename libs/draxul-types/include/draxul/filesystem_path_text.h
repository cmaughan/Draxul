#pragma once

#include <string_view>

namespace draxul
{

[[nodiscard]] inline std::string_view path_display_basename(std::string_view path)
{
    while (path.size() > 1 && (path.back() == '/' || path.back() == '\\'))
        path.remove_suffix(1);
    const size_t separator = path.find_last_of("/\\");
    return separator == std::string_view::npos ? path : path.substr(separator + 1);
}

} // namespace draxul
