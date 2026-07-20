#include <draxul/satview/satview_label_layout.h>

#include <algorithm>
#include <glm/common.hpp>

namespace draxul::satview
{

namespace
{

struct PixelRect
{
    glm::vec2 minimum{ 0.0f };
    glm::vec2 maximum{ 0.0f };
};

bool overlaps(const PixelRect& lhs, const PixelRect& rhs)
{
    return lhs.minimum.x < rhs.maximum.x && lhs.maximum.x > rhs.minimum.x
        && lhs.minimum.y < rhs.maximum.y && lhs.maximum.y > rhs.minimum.y;
}

} // namespace

std::vector<SatViewLabelInstance> layout_satview_labels(
    std::vector<SatViewLabelLayoutCandidate> candidates,
    glm::ivec2 viewport_size,
    std::span<const SatViewLabelLayoutExclusion> exclusions,
    float padding_pixels)
{
    if (viewport_size.x <= 0 || viewport_size.y <= 0)
        return {};

    std::sort(candidates.begin(), candidates.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.rank != rhs.rank)
            return lhs.rank < rhs.rank;
        return lhs.key < rhs.key;
    });

    const glm::vec2 viewport(viewport_size);
    std::vector<PixelRect> accepted_rects;
    accepted_rects.reserve(exclusions.size() + candidates.size());
    for (const SatViewLabelLayoutExclusion& exclusion : exclusions)
    {
        const glm::vec2 center = (exclusion.ndc_center * 0.5f + 0.5f) * viewport;
        const glm::vec2 half_size = glm::max(exclusion.pixel_size * 0.5f, glm::vec2(0.5f));
        accepted_rects.push_back({ center - half_size, center + half_size });
    }
    std::vector<SatViewLabelInstance> accepted;
    accepted.reserve(candidates.size());
    for (const SatViewLabelLayoutCandidate& candidate : candidates)
    {
        const glm::vec2 center = (candidate.ndc_center * 0.5f + 0.5f) * viewport;
        const glm::vec2 half_size = glm::max(
            glm::vec2(candidate.instance.pixel_size_offset) * 0.5f,
            glm::vec2(0.5f));
        const PixelRect rect{
            center - half_size - padding_pixels,
            center + half_size + padding_pixels,
        };
        if (rect.maximum.x < 0.0f || rect.maximum.y < 0.0f
            || rect.minimum.x > viewport.x || rect.minimum.y > viewport.y)
        {
            continue;
        }
        if (std::ranges::any_of(accepted_rects, [&](const PixelRect& accepted_rect) {
                return overlaps(rect, accepted_rect);
            }))
        {
            continue;
        }
        accepted_rects.push_back(rect);
        accepted.push_back(candidate.instance);
    }
    return accepted;
}

} // namespace draxul::satview
