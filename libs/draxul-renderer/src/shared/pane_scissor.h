#pragma once

// Pane scissor clamping — one definition for both renderer backends.
//
// The same arithmetic existed three times (Metal's grid draw, Metal's render
// pass, Vulkan's grid draw) and one of the three had drifted: the Metal grid
// draw was missing the std::max(0, ...) on the extents, so a pane whose origin
// lies past the right or bottom edge produced a negative extent that became an
// enormous value when cast to NSUInteger (audit bug #8). Both platforms' scissor
// extents are unsigned, so the clamp is load-bearing on both.
//
// Backend-private, like grid_contract.h: kept free of Metal/Vulkan headers so
// both backends and the plain-C++ unit tests can include it.

#include <algorithm>

namespace draxul::pane_scissor
{

// A scissor rectangle in window pixels, already clamped to the surface.
// Every field is non-negative, so casting to an unsigned platform type is safe.
struct ScissorRect
{
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    [[nodiscard]] bool empty() const
    {
        return width <= 0 || height <= 0;
    }
};

// Clamps a pane rectangle to the [0, surface] range on both axes.
//
// A negative origin is pulled to 0 WITHOUT growing the extent, matching the
// behaviour both backends already had: the pane's visible part starts at the
// edge, and the off-screen columns/rows are simply cropped. An origin at or
// beyond the far edge, or a non-positive size, yields a zero extent rather than
// an underflowed one.
inline ScissorRect clamp(int pane_x, int pane_y, int pane_width, int pane_height,
    int surface_width, int surface_height)
{
    ScissorRect rect;
    rect.x = std::max(0, pane_x);
    rect.y = std::max(0, pane_y);
    rect.width = std::max(0, std::min(pane_width, surface_width - rect.x));
    rect.height = std::max(0, std::min(pane_height, surface_height - rect.y));
    return rect;
}

} // namespace draxul::pane_scissor
