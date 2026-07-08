#pragma once

#include <array>
#include <cctype>
#include <cstdint>
#include <draxul/pastel_palette.h>
#include <glm/vec4.hpp>
#include <string>
#include <string_view>

namespace draxul::satview
{

inline constexpr std::array<glm::vec4, 24> kSatelliteAccentPalette = {
    glm::vec4(0.537f, 0.863f, 0.922f, 1.0f),
    glm::vec4(0.651f, 0.890f, 0.631f, 1.0f),
    glm::vec4(0.706f, 0.745f, 0.996f, 1.0f),
    glm::vec4(0.976f, 0.886f, 0.686f, 1.0f),
    glm::vec4(0.580f, 0.886f, 0.835f, 1.0f),
    glm::vec4(0.502f, 0.827f, 0.890f, 1.0f),
    glm::vec4(0.980f, 0.702f, 0.529f, 1.0f),
    glm::vec4(0.455f, 0.780f, 0.925f, 1.0f),
    glm::vec4(0.765f, 0.890f, 0.502f, 1.0f),
    glm::vec4(0.584f, 0.647f, 0.890f, 1.0f),
    glm::vec4(0.502f, 0.745f, 0.682f, 1.0f),
    glm::vec4(0.537f, 0.706f, 0.980f, 1.0f),
    glm::vec4(0.827f, 0.827f, 0.584f, 1.0f),
    glm::vec4(0.620f, 0.835f, 0.965f, 1.0f),
    glm::vec4(0.749f, 0.918f, 0.718f, 1.0f),
    glm::vec4(0.855f, 0.733f, 0.502f, 1.0f),
    glm::vec4(0.651f, 0.780f, 0.980f, 1.0f),
    glm::vec4(0.678f, 0.902f, 0.820f, 1.0f),
    glm::vec4(0.949f, 0.820f, 0.580f, 1.0f),
    glm::vec4(0.796f, 0.651f, 0.969f, 1.0f),
    glm::vec4(0.702f, 0.878f, 0.980f, 1.0f),
    glm::vec4(0.890f, 0.643f, 0.584f, 1.0f),
    glm::vec4(0.643f, 0.827f, 0.502f, 1.0f),
    glm::vec4(0.749f, 0.675f, 0.925f, 1.0f),
};

inline bool is_object_prefix_separator(char ch)
{
    return ch == ' ' || ch == '\t' || ch == '-' || ch == '_' || ch == '/' || ch == '(' || ch == '[' || ch == '#';
}

inline std::string normalized_satellite_prefix(std::string_view object_name)
{
    const auto not_space = [](unsigned char ch) {
        return !std::isspace(ch);
    };
    while (!object_name.empty() && !not_space(static_cast<unsigned char>(object_name.front())))
        object_name.remove_prefix(1);
    while (!object_name.empty() && !not_space(static_cast<unsigned char>(object_name.back())))
        object_name.remove_suffix(1);
    if (object_name.empty())
        return "Other";

    std::size_t end = std::string_view::npos;
    for (std::size_t i = 0; i < object_name.size(); ++i)
    {
        if (std::isdigit(static_cast<unsigned char>(object_name[i])))
        {
            end = i;
            break;
        }
    }
    if (end == std::string_view::npos)
    {
        for (std::size_t i = 1; i < object_name.size(); ++i)
        {
            if (is_object_prefix_separator(object_name[i]))
            {
                end = i;
                break;
            }
        }
    }
    if (end == std::string_view::npos)
        return "Other";

    while (end > 0 && is_object_prefix_separator(object_name[end - 1]))
        --end;
    while (end > 0 && !std::isalnum(static_cast<unsigned char>(object_name[end - 1])))
        --end;
    if (end == 0)
        return "Other";

    bool has_alpha = false;
    std::string prefix;
    prefix.reserve(end);
    bool previous_space = false;
    for (std::size_t i = 0; i < end; ++i)
    {
        const unsigned char ch = static_cast<unsigned char>(object_name[i]);
        if (std::isalpha(ch))
            has_alpha = true;
        if (std::isspace(ch))
        {
            if (!previous_space && !prefix.empty())
                prefix.push_back(' ');
            previous_space = true;
            continue;
        }
        previous_space = false;
        prefix.push_back(static_cast<char>(std::toupper(ch)));
    }
    while (!prefix.empty() && prefix.back() == ' ')
        prefix.pop_back();
    if (!has_alpha || prefix.empty())
        return "Other";
    return prefix;
}

inline std::uint32_t satellite_prefix_hash(std::string_view object_name)
{
    return stable_color_hash(normalized_satellite_prefix(object_name));
}

inline glm::vec4 satellite_prefix_color(std::uint32_t prefix_hash, float alpha = 1.0f)
{
    glm::vec4 color = kSatelliteAccentPalette[prefix_hash % kSatelliteAccentPalette.size()];
    color.a = alpha;
    return color;
}

inline glm::vec4 selected_marker_color(float alpha = 1.0f)
{
    return glm::vec4(1.0f, 0.96f, 0.68f, alpha);
}

} // namespace draxul::satview
