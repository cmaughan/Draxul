#pragma once

#include <filesystem>
#include <string>
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

// Draxul carries paths between processes, CLI arguments, launch options, and
// environment variables as UTF-8 text. On Windows, path(std::string) and
// path::string() use the active code page instead, which garbles or throws on
// non-ASCII names, so cross those boundaries with these helpers.
[[nodiscard]] inline std::filesystem::path path_from_utf8(std::string_view text)
{
    return std::filesystem::path(
        std::u8string(text.begin(), text.end()));
}

[[nodiscard]] inline std::string path_to_utf8(const std::filesystem::path& path)
{
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

} // namespace draxul
