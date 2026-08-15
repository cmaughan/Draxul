#pragma once

#include <draxul/codeviz_scene_types.h>

#include <draxul/text_atlas.h>
#include <draxul/text_atlas_builder.h>

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace draxul
{

using SignAtlasEntry = TextAtlasEntry;

enum class SignLabelVerticalAlign
{
    Center,
    Top,
};

struct SignLabelRequest
{
    std::string key;
    std::string text;
    int target_pixel_width = 1;
    int target_pixel_height = 1;
    bool align_primary_to_start = false;
    SignLabelVerticalAlign vertical_align = SignLabelVerticalAlign::Center;
    uint8_t text_r = 0;
    uint8_t text_g = 0;
    uint8_t text_b = 0;
};

struct SignLabelAtlas : TextAtlas
{
};

[[nodiscard]] std::shared_ptr<SignLabelAtlas> build_sign_label_atlas(
    TextService& text_service, const std::vector<SignLabelRequest>& requests, uint64_t revision);

} // namespace draxul
