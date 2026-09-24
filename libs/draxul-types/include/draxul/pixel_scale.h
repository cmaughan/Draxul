#pragma once

#include <cmath>

namespace draxul
{

// Encapsulates the physical-to-logical pixel ratio (e.g. 2.0 on Retina displays).
// Provides canonical conversion helpers so that inline `* scale` / `/ scale` patterns
// are not reinvented at every call site.
struct PixelScale
{
    float scale = 1.0f;

    PixelScale() = default;

    explicit PixelScale(float s)
        : scale(s)
    {
    }

    // Raw value accessor for APIs that take a float.
    float value() const
    {
        return scale;
    }

    // Convert a logical-space integer coordinate to physical pixels (rounded).
    int to_physical(int logical) const
    {
        return static_cast<int>(std::round(static_cast<float>(logical) * scale));
    }

    // Convert a logical-space float coordinate to physical pixels.
    float to_physical(float logical) const
    {
        return logical * scale;
    }

    // Convert a physical-space integer coordinate to logical pixels (rounded).
    int to_logical(int physical) const
    {
        return scale > 0.0f ? static_cast<int>(std::round(static_cast<float>(physical) / scale)) : physical;
    }

    // Convert a physical-space float coordinate to logical pixels.
    float to_logical(float physical) const
    {
        return scale > 0.0f ? physical / scale : physical;
    }

    // Compute the pixel scale from window dimensions.
    static PixelScale from_window(int pixel_w, int logical_w)
    {
        return logical_w > 0 ? PixelScale(static_cast<float>(pixel_w) / static_cast<float>(logical_w))
                             : PixelScale(1.0f);
    }
};

} // namespace draxul
