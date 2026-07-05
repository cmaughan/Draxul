#pragma once

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>
#include <span>
#include <string>
#include <vector>

namespace draxul::satview
{

struct SatViewLabelInstance
{
    glm::vec4 direction_priority{ 0.0f, 1.0f, 0.0f, 0.0f };
    glm::vec4 uv_rect{ 0.0f, 0.0f, 1.0f, 1.0f };
    glm::vec4 pixel_size_offset{ 1.0f, 1.0f, 0.0f, 0.0f };
    glm::vec4 color{ 1.0f };
};

struct SatViewLabelLayoutCandidate
{
    std::string key;
    SatViewLabelInstance instance;
    glm::vec2 ndc_center{ 0.0f };
    int rank = 3;
};

struct SatViewLabelLayoutExclusion
{
    glm::vec2 ndc_center{ 0.0f };
    glm::vec2 pixel_size{ 0.0f };
};

[[nodiscard]] std::vector<SatViewLabelInstance> layout_satview_labels(
    std::vector<SatViewLabelLayoutCandidate> candidates,
    glm::ivec2 viewport_size,
    std::span<const SatViewLabelLayoutExclusion> exclusions = {},
    float padding_pixels = 4.0f);

} // namespace draxul::satview
