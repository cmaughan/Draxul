// Pure unit tests for the render-test image comparison (kanban 16).
//
// compare_frames() gates every render snapshot: a bug here can make all scenario
// results falsely pass or fail. These tests pin the threshold/dimension/tolerance
// branches deterministically, with no GPU, window, or Neovim.

#include <catch2/catch_test_macros.hpp>

#include <draxul/render_test.h>
#include <draxul/types.h>

#include <cstdint>
#include <vector>

using draxul::CapturedFrame;
using draxul::compare_frames;
using draxul::RenderDiff;

namespace
{
// A w*h frame filled with one RGBA color.
CapturedFrame solid(int w, int h, std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a)
{
    CapturedFrame f;
    f.width = w;
    f.height = h;
    f.rgba.reserve(static_cast<size_t>(w) * h * 4);
    for (int i = 0; i < w * h; ++i)
    {
        f.rgba.push_back(r);
        f.rgba.push_back(g);
        f.rgba.push_back(b);
        f.rgba.push_back(a);
    }
    return f;
}
} // namespace

TEST_CASE("compare_frames: identical frames pass with zero difference", "[render_compare]")
{
    const auto a = solid(4, 4, 30, 60, 90, 255);
    const RenderDiff d = compare_frames(a, a, /*tolerance=*/0, /*threshold_pct=*/0.0);
    REQUIRE(d.passed);
    REQUIRE(d.changed_pixels == 0);
    REQUIRE(d.changed_pixels_pct == 0.0);
    REQUIRE(d.max_channel_diff == 0);
    REQUIRE(d.mean_abs_channel_diff == 0.0);
    // The diff image mirrors the compared dimensions.
    REQUIRE(d.diff_image.width == 4);
    REQUIRE(d.diff_image.height == 4);
}

TEST_CASE("compare_frames: dimension mismatch never passes", "[render_compare]")
{
    const auto a = solid(4, 4, 10, 10, 10, 255);
    const auto ref = solid(4, 5, 10, 10, 10, 255); // same pixels, different height
    const RenderDiff d = compare_frames(a, ref, /*tolerance=*/8, /*threshold_pct=*/100.0);
    // Even at a 100% threshold, a size mismatch must fail (no comparison performed).
    REQUIRE_FALSE(d.passed);
    REQUIRE(d.changed_pixels == 0);
    REQUIRE(d.diff_image.width == 4); // diff image tracks the actual frame
    REQUIRE(d.diff_image.height == 4);
}

TEST_CASE("compare_frames: an invalid frame never passes", "[render_compare]")
{
    auto a = solid(2, 2, 0, 0, 0, 255);
    a.rgba.pop_back(); // rgba no longer w*h*4 -> valid() is false
    const auto ref = solid(2, 2, 0, 0, 0, 255);
    const RenderDiff d = compare_frames(a, ref, 0, 100.0);
    REQUIRE_FALSE(d.passed);
    REQUIRE(d.changed_pixels == 0);
}

TEST_CASE("compare_frames: channel order matters (a red/blue swap is a difference)", "[render_compare]")
{
    const auto red = solid(1, 1, 255, 0, 0, 255);
    const auto blue = solid(1, 1, 0, 0, 255, 255);
    const RenderDiff d = compare_frames(red, blue, /*tolerance=*/8, /*threshold_pct=*/0.0);
    REQUIRE_FALSE(d.passed);
    REQUIRE(d.changed_pixels == 1);
    REQUIRE(d.max_channel_diff == 255);
    // |255-0| on R and B, 0 on G and A -> (255+0+255+0)/4.
    REQUIRE(d.mean_abs_channel_diff == 127.5);
    // Diff image stores per-channel absolute deltas with opaque alpha.
    REQUIRE(d.diff_image.rgba.size() == 4);
    REQUIRE(d.diff_image.rgba[0] == 255); // R delta
    REQUIRE(d.diff_image.rgba[1] == 0); // G delta
    REQUIRE(d.diff_image.rgba[2] == 255); // B delta
    REQUIRE(d.diff_image.rgba[3] == 255); // alpha forced opaque
}

TEST_CASE("compare_frames: per-pixel tolerance is a strict greater-than", "[render_compare]")
{
    const auto a = solid(1, 1, 18, 0, 0, 255); // R delta of 8 vs reference
    const auto ref = solid(1, 1, 10, 0, 0, 255);

    SECTION("delta equal to tolerance is NOT a changed pixel")
    {
        const RenderDiff d = compare_frames(a, ref, /*tolerance=*/8, /*threshold_pct=*/0.0);
        REQUIRE(d.changed_pixels == 0);
        REQUIRE(d.passed);
        REQUIRE(d.max_channel_diff == 8); // still recorded as the peak delta
    }
    SECTION("delta one above tolerance IS a changed pixel")
    {
        const RenderDiff d = compare_frames(a, ref, /*tolerance=*/7, /*threshold_pct=*/0.0);
        REQUIRE(d.changed_pixels == 1);
        REQUIRE_FALSE(d.passed);
    }
}

TEST_CASE("compare_frames: changed-pixel threshold is inclusive (<=)", "[render_compare]")
{
    // 10x10 = 100 pixels; exactly one differs beyond tolerance -> 1.0%.
    auto a = solid(10, 10, 0, 0, 0, 255);
    const auto ref = solid(10, 10, 0, 0, 0, 255);
    a.rgba[0] = 200; // one channel of one pixel far exceeds any small tolerance

    const RenderDiff base = compare_frames(a, ref, /*tolerance=*/8, /*threshold_pct=*/0.0);
    REQUIRE(base.changed_pixels == 1);
    REQUIRE(base.changed_pixels_pct == 1.0);

    SECTION("threshold below the ratio fails")
    {
        REQUIRE_FALSE(compare_frames(a, ref, 8, 0.5).passed);
    }
    SECTION("threshold exactly at the ratio passes (inclusive)")
    {
        REQUIRE(compare_frames(a, ref, 8, 1.0).passed);
    }
    SECTION("threshold above the ratio passes")
    {
        REQUIRE(compare_frames(a, ref, 8, 2.0).passed);
    }
    SECTION("zero threshold fails on any changed pixel")
    {
        REQUIRE_FALSE(compare_frames(a, ref, 8, 0.0).passed);
    }
}

TEST_CASE("compare_frames: fully different frames report 100% changed", "[render_compare]")
{
    const auto black = solid(8, 8, 0, 0, 0, 255);
    const auto white = solid(8, 8, 255, 255, 255, 255);
    const RenderDiff d = compare_frames(black, white, /*tolerance=*/8, /*threshold_pct=*/0.0);
    REQUIRE(d.changed_pixels == 64);
    REQUIRE(d.changed_pixels_pct == 100.0);
    REQUIRE(d.max_channel_diff == 255);
    REQUIRE_FALSE(d.passed);
}

TEST_CASE("compare_frames: zero-size frames do not divide by zero", "[render_compare]")
{
    const CapturedFrame empty; // width=height=0, empty rgba -> invalid
    const RenderDiff d = compare_frames(empty, empty, 0, 0.0);
    REQUIRE_FALSE(d.passed); // invalid frames never pass
    REQUIRE(d.changed_pixels == 0);
    REQUIRE(d.changed_pixels_pct == 0.0);
    REQUIRE(d.mean_abs_channel_diff == 0.0);
}
