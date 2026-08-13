#include <draxul/module_path_resolver.h>

#include <algorithm>

namespace draxul
{

std::string module_path_for_source_file(std::string_view file_path)
{
    std::string normalized(file_path);
    std::replace(normalized.begin(), normalized.end(), '\\', '/');

    if (normalized.empty())
        return {};

    const auto first_slash = normalized.find('/');
    if (first_slash == std::string::npos)
        return normalized.find('.') == std::string::npos ? normalized : ".";

    const std::string_view first(normalized.data(), first_slash);
    if (first == "libs" || first == "modules")
    {
        const auto second_slash = normalized.find('/', first_slash + 1);
        return second_slash == std::string::npos
            ? normalized
            : normalized.substr(0, second_slash);
    }

    return std::string(first);
}

} // namespace draxul
