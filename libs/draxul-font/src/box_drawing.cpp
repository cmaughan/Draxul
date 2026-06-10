#include "box_drawing.h"

#include <draxul/unicode.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace draxul
{

namespace
{

struct Canvas
{
    int w = 0;
    int h = 0;
    std::vector<uint8_t>& alpha;

    // Saturating-additive: primitives sharing an anti-aliased edge (e.g. the
    // quadrants of ▟) must sum their partial coverage, not take the max.
    void blend(int x, int y, float coverage)
    {
        if (x < 0 || x >= w || y < 0 || y >= h)
            return;
        const int value = (int)std::lround(std::clamp(coverage, 0.0f, 1.0f) * 255.0f);
        auto& dst = alpha[(size_t)y * w + x];
        dst = (uint8_t)std::min((int)dst + value, 255);
    }

    // Axis-aligned rectangle with anti-aliased fractional edges.
    void fill_rect(float x0, float y0, float x1, float y1, float max_coverage = 1.0f)
    {
        x0 = std::max(x0, 0.0f);
        y0 = std::max(y0, 0.0f);
        x1 = std::min(x1, (float)w);
        y1 = std::min(y1, (float)h);
        if (x1 <= x0 || y1 <= y0)
            return;

        const int px0 = (int)std::floor(x0);
        const int px1 = (int)std::ceil(x1);
        const int py0 = (int)std::floor(y0);
        const int py1 = (int)std::ceil(y1);
        for (int y = py0; y < py1; y++)
        {
            const float cov_y = std::min(y1, (float)(y + 1)) - std::max(y0, (float)y);
            for (int x = px0; x < px1; x++)
            {
                const float cov_x = std::min(x1, (float)(x + 1)) - std::max(x0, (float)x);
                blend(x, y, cov_x * cov_y * max_coverage);
            }
        }
    }

    // Anti-aliased thick line segment.
    void stroke_segment(float ax, float ay, float bx, float by, float thickness)
    {
        const float half = thickness * 0.5f;
        const int px0 = (int)std::floor(std::min(ax, bx) - half - 1.0f);
        const int px1 = (int)std::ceil(std::max(ax, bx) + half + 1.0f);
        const int py0 = (int)std::floor(std::min(ay, by) - half - 1.0f);
        const int py1 = (int)std::ceil(std::max(ay, by) + half + 1.0f);
        const float dx = bx - ax;
        const float dy = by - ay;
        const float len_sq = std::max(dx * dx + dy * dy, 1e-6f);
        for (int y = std::max(py0, 0); y < std::min(py1, h); y++)
        {
            for (int x = std::max(px0, 0); x < std::min(px1, w); x++)
            {
                const float px = x + 0.5f;
                const float py = y + 0.5f;
                const float t = std::clamp(((px - ax) * dx + (py - ay) * dy) / len_sq, 0.0f, 1.0f);
                const float qx = ax + t * dx - px;
                const float qy = ay + t * dy - py;
                const float dist = std::sqrt(qx * qx + qy * qy);
                blend(x, y, half + 0.5f - dist);
            }
        }
    }

    // Anti-aliased quarter-circle arc stroke. quadrant_x/quadrant_y select
    // which quadrant relative to the arc center is drawn (-1 or +1).
    void stroke_arc(float center_x, float center_y, float radius, float thickness,
        int quadrant_x, int quadrant_y)
    {
        const float half = thickness * 0.5f;
        const int px0 = (int)std::floor(center_x - radius - half - 1.0f);
        const int px1 = (int)std::ceil(center_x + radius + half + 1.0f);
        const int py0 = (int)std::floor(center_y - radius - half - 1.0f);
        const int py1 = (int)std::ceil(center_y + radius + half + 1.0f);
        for (int y = std::max(py0, 0); y < std::min(py1, h); y++)
        {
            for (int x = std::max(px0, 0); x < std::min(px1, w); x++)
            {
                const float rx = x + 0.5f - center_x;
                const float ry = y + 0.5f - center_y;
                if (rx * (float)quadrant_x < 0.0f || ry * (float)quadrant_y < 0.0f)
                    continue;
                const float dist = std::abs(std::sqrt(rx * rx + ry * ry) - radius);
                blend(x, y, half + 0.5f - dist);
            }
        }
    }
};

int snap(float v)
{
    return (int)std::lround(v);
}

// Arm thickness: 0 = none, 1 = light, 2 = heavy.
struct Arms
{
    uint8_t up = 0;
    uint8_t down = 0;
    uint8_t left = 0;
    uint8_t right = 0;
};

// Box drawing weights for U+250C..U+254B (corners, tees, crosses).
// Encoded as {up, down, left, right} with 1 = light, 2 = heavy.
constexpr Arms kJunctionArms[] = {
    { 0, 1, 0, 1 },
    { 0, 1, 0, 2 },
    { 0, 2, 0, 1 },
    { 0, 2, 0, 2 }, // 250C ┌ ┍ ┎ ┏
    { 0, 1, 1, 0 },
    { 0, 1, 2, 0 },
    { 0, 2, 1, 0 },
    { 0, 2, 2, 0 }, // 2510 ┐ ┑ ┒ ┓
    { 1, 0, 0, 1 },
    { 1, 0, 0, 2 },
    { 2, 0, 0, 1 },
    { 2, 0, 0, 2 }, // 2514 └ ┕ ┖ ┗
    { 1, 0, 1, 0 },
    { 1, 0, 2, 0 },
    { 2, 0, 1, 0 },
    { 2, 0, 2, 0 }, // 2518 ┘ ┙ ┚ ┛
    { 1, 1, 0, 1 },
    { 1, 1, 0, 2 },
    { 2, 1, 0, 1 },
    { 1, 2, 0, 1 }, // 251C ├ ┝ ┞ ┟
    { 2, 2, 0, 1 },
    { 2, 1, 0, 2 },
    { 1, 2, 0, 2 },
    { 2, 2, 0, 2 }, // 2520 ┠ ┡ ┢ ┣
    { 1, 1, 1, 0 },
    { 1, 1, 2, 0 },
    { 2, 1, 1, 0 },
    { 1, 2, 1, 0 }, // 2524 ┤ ┥ ┦ ┧
    { 2, 2, 1, 0 },
    { 2, 1, 2, 0 },
    { 1, 2, 2, 0 },
    { 2, 2, 2, 0 }, // 2528 ┨ ┩ ┪ ┫
    { 0, 1, 1, 1 },
    { 0, 1, 2, 1 },
    { 0, 1, 1, 2 },
    { 0, 1, 2, 2 }, // 252C ┬ ┭ ┮ ┯
    { 0, 2, 1, 1 },
    { 0, 2, 2, 1 },
    { 0, 2, 1, 2 },
    { 0, 2, 2, 2 }, // 2530 ┰ ┱ ┲ ┳
    { 1, 0, 1, 1 },
    { 1, 0, 2, 1 },
    { 1, 0, 1, 2 },
    { 1, 0, 2, 2 }, // 2534 ┴ ┵ ┶ ┷
    { 2, 0, 1, 1 },
    { 2, 0, 2, 1 },
    { 2, 0, 1, 2 },
    { 2, 0, 2, 2 }, // 2538 ┸ ┹ ┺ ┻
    { 1, 1, 1, 1 },
    { 1, 1, 2, 1 },
    { 1, 1, 1, 2 },
    { 1, 1, 2, 2 }, // 253C ┼ ┽ ┾ ┿
    { 2, 1, 1, 1 },
    { 1, 2, 1, 1 },
    { 2, 2, 1, 1 },
    { 2, 1, 2, 1 }, // 2540 ╀ ╁ ╂ ╃
    { 2, 1, 1, 2 },
    { 1, 2, 2, 1 },
    { 1, 2, 1, 2 },
    { 2, 1, 2, 2 }, // 2544 ╄ ╅ ╆ ╇
    { 1, 2, 2, 2 },
    { 2, 2, 2, 1 },
    { 2, 2, 1, 2 },
    { 2, 2, 2, 2 }, // 2548 ╈ ╉ ╊ ╋
};

// Half lines and mixed half lines U+2574..U+257F.
constexpr Arms kHalfLineArms[] = {
    { 0, 0, 1, 0 },
    { 1, 0, 0, 0 },
    { 0, 0, 0, 1 },
    { 0, 1, 0, 0 }, // 2574 ╴ ╵ ╶ ╷
    { 0, 0, 2, 0 },
    { 2, 0, 0, 0 },
    { 0, 0, 0, 2 },
    { 0, 2, 0, 0 }, // 2578 ╸ ╹ ╺ ╻
    { 0, 0, 1, 2 },
    { 1, 2, 0, 0 },
    { 0, 0, 2, 1 },
    { 2, 1, 0, 0 }, // 257C ╼ ╽ ╾ ╿
};

struct BoxGeometry
{
    int w;
    int h;
    float cx;
    float cy;
    int light; // light stroke thickness in px
    int heavy; // heavy stroke thickness in px

    int thickness(int weight) const
    {
        return weight == 2 ? heavy : light;
    }
};

void draw_h_band(Canvas& canvas, float x0, float x1, float yc, int t)
{
    const int y0 = snap(yc - t * 0.5f);
    canvas.fill_rect(x0, (float)y0, x1, (float)(y0 + t));
}

void draw_v_band(Canvas& canvas, float y0, float y1, float xc, int t)
{
    const int x0 = snap(xc - t * 0.5f);
    canvas.fill_rect((float)x0, y0, (float)(x0 + t), y1);
}

void draw_arms(Canvas& canvas, const BoxGeometry& geo, const Arms& arms)
{
    // Each arm extends past the centre far enough to cover the thickest
    // crossing band so mixed light/heavy junctions join cleanly.
    const int v_band = std::max(arms.up ? geo.thickness(arms.up) : 0,
        arms.down ? geo.thickness(arms.down) : 0);
    const int h_band = std::max(arms.left ? geo.thickness(arms.left) : 0,
        arms.right ? geo.thickness(arms.right) : 0);

    if (arms.left)
    {
        const int t = geo.thickness(arms.left);
        const int cover = v_band > 0 ? v_band : t;
        draw_h_band(canvas, 0.0f, (float)snap(geo.cx + cover * 0.5f), geo.cy, t);
    }
    if (arms.right)
    {
        const int t = geo.thickness(arms.right);
        const int cover = v_band > 0 ? v_band : t;
        draw_h_band(canvas, (float)snap(geo.cx - cover * 0.5f), (float)geo.w, geo.cy, t);
    }
    if (arms.up)
    {
        const int t = geo.thickness(arms.up);
        const int cover = h_band > 0 ? h_band : t;
        draw_v_band(canvas, 0.0f, (float)snap(geo.cy + cover * 0.5f), geo.cx, t);
    }
    if (arms.down)
    {
        const int t = geo.thickness(arms.down);
        const int cover = h_band > 0 ? h_band : t;
        draw_v_band(canvas, (float)snap(geo.cy - cover * 0.5f), (float)geo.h, geo.cx, t);
    }
}

void draw_dashed(Canvas& canvas, const BoxGeometry& geo, int dash_count, bool vertical, int weight)
{
    const int t = geo.thickness(weight);
    const float span = vertical ? (float)geo.h : (float)geo.w;
    const float segment = span / (float)dash_count;
    const float dash = segment * 0.7f;
    for (int i = 0; i < dash_count; i++)
    {
        const float start = i * segment + (segment - dash) * 0.5f;
        if (vertical)
            draw_v_band(canvas, start, start + dash, geo.cx, t);
        else
            draw_h_band(canvas, start, start + dash, geo.cy, t);
    }
}

void draw_double_lines(Canvas& canvas, const BoxGeometry& geo, uint32_t cp)
{
    const int t = geo.light;
    const float cx = geo.cx;
    const float cy = geo.cy;
    const float w = (float)geo.w;
    const float h = (float)geo.h;
    // Parallel rail centres, far enough apart to leave a clear gap.
    const float gap = (float)t;
    const float xl = cx - gap;
    const float xr = cx + gap;
    const float yt = cy - gap;
    const float yb = cy + gap;

    auto hline = [&](float yc, float x0, float x1) { draw_h_band(canvas, x0, x1, yc, t); };
    auto vline = [&](float xc, float y0, float y1) { draw_v_band(canvas, y0, y1, xc, t); };
    // Band edges of a rail centred at c, used so meeting segments form a
    // clean square corner.
    auto lo = [&](float c) { return c - t * 0.5f; };
    auto hi = [&](float c) { return c + t * 0.5f; };

    switch (cp)
    {
    case 0x2550: // ═
        hline(yt, 0, w);
        hline(yb, 0, w);
        break;
    case 0x2551: // ║
        vline(xl, 0, h);
        vline(xr, 0, h);
        break;
    case 0x2552: // ╒
        hline(yt, lo(cx), w);
        hline(yb, lo(cx), w);
        vline(cx, lo(yt), h);
        break;
    case 0x2553: // ╓
        vline(xl, lo(cy), h);
        vline(xr, lo(cy), h);
        hline(cy, lo(xl), w);
        break;
    case 0x2554: // ╔
        hline(yt, lo(xl), w);
        hline(yb, lo(xr), w);
        vline(xl, lo(yt), h);
        vline(xr, lo(yb), h);
        break;
    case 0x2555: // ╕
        hline(yt, 0, hi(cx));
        hline(yb, 0, hi(cx));
        vline(cx, lo(yt), h);
        break;
    case 0x2556: // ╖
        vline(xl, lo(cy), h);
        vline(xr, lo(cy), h);
        hline(cy, 0, hi(xr));
        break;
    case 0x2557: // ╗
        hline(yt, 0, hi(xr));
        hline(yb, 0, hi(xl));
        vline(xr, lo(yt), h);
        vline(xl, lo(yb), h);
        break;
    case 0x2558: // ╘
        hline(yt, lo(cx), w);
        hline(yb, lo(cx), w);
        vline(cx, 0, hi(yb));
        break;
    case 0x2559: // ╙
        vline(xl, 0, hi(cy));
        vline(xr, 0, hi(cy));
        hline(cy, lo(xl), w);
        break;
    case 0x255A: // ╚
        vline(xl, 0, hi(yb));
        vline(xr, 0, hi(yt));
        hline(yb, lo(xl), w);
        hline(yt, lo(xr), w);
        break;
    case 0x255B: // ╛
        hline(yt, 0, hi(cx));
        hline(yb, 0, hi(cx));
        vline(cx, 0, hi(yb));
        break;
    case 0x255C: // ╜
        vline(xl, 0, hi(cy));
        vline(xr, 0, hi(cy));
        hline(cy, 0, hi(xr));
        break;
    case 0x255D: // ╝
        vline(xr, 0, hi(yb));
        vline(xl, 0, hi(yt));
        hline(yb, 0, hi(xr));
        hline(yt, 0, hi(xl));
        break;
    case 0x255E: // ╞
        vline(cx, 0, h);
        hline(yt, lo(cx), w);
        hline(yb, lo(cx), w);
        break;
    case 0x255F: // ╟
        vline(xl, 0, h);
        vline(xr, 0, h);
        hline(cy, lo(xr), w);
        break;
    case 0x2560: // ╠
        vline(xl, 0, h);
        vline(xr, 0, hi(yt));
        vline(xr, lo(yb), h);
        hline(yt, lo(xr), w);
        hline(yb, lo(xr), w);
        break;
    case 0x2561: // ╡
        vline(cx, 0, h);
        hline(yt, 0, hi(cx));
        hline(yb, 0, hi(cx));
        break;
    case 0x2562: // ╢
        vline(xl, 0, h);
        vline(xr, 0, h);
        hline(cy, 0, hi(xl));
        break;
    case 0x2563: // ╣
        vline(xr, 0, h);
        vline(xl, 0, hi(yt));
        vline(xl, lo(yb), h);
        hline(yt, 0, hi(xl));
        hline(yb, 0, hi(xl));
        break;
    case 0x2564: // ╤
        hline(yt, 0, w);
        hline(yb, 0, w);
        vline(cx, lo(yb), h);
        break;
    case 0x2565: // ╥
        hline(cy, 0, w);
        vline(xl, lo(cy), h);
        vline(xr, lo(cy), h);
        break;
    case 0x2566: // ╦
        hline(yt, 0, w);
        hline(yb, 0, hi(xl));
        hline(yb, lo(xr), w);
        vline(xl, lo(yb), h);
        vline(xr, lo(yb), h);
        break;
    case 0x2567: // ╧
        hline(yt, 0, w);
        hline(yb, 0, w);
        vline(cx, 0, hi(yt));
        break;
    case 0x2568: // ╨
        hline(cy, 0, w);
        vline(xl, 0, hi(cy));
        vline(xr, 0, hi(cy));
        break;
    case 0x2569: // ╩
        hline(yb, 0, w);
        hline(yt, 0, hi(xl));
        hline(yt, lo(xr), w);
        vline(xl, 0, hi(yt));
        vline(xr, 0, hi(yt));
        break;
    case 0x256A: // ╪
        vline(cx, 0, h);
        hline(yt, 0, w);
        hline(yb, 0, w);
        break;
    case 0x256B: // ╫
        hline(cy, 0, w);
        vline(xl, 0, h);
        vline(xr, 0, h);
        break;
    case 0x256C: // ╬
        vline(xl, 0, hi(yt));
        vline(xl, lo(yb), h);
        vline(xr, 0, hi(yt));
        vline(xr, lo(yb), h);
        hline(yt, 0, hi(xl));
        hline(yt, lo(xr), w);
        hline(yb, 0, hi(xl));
        hline(yb, lo(xr), w);
        break;
    default:
        break;
    }
}

void draw_rounded_corner(Canvas& canvas, const BoxGeometry& geo, uint32_t cp)
{
    const float t = (float)geo.light;
    const float cx = geo.cx;
    const float cy = geo.cy;
    const float w = (float)geo.w;
    const float h = (float)geo.h;
    const float radius = std::min(cx, cy);

    switch (cp)
    {
    case 0x256D: // ╭ arms down + right
        draw_v_band(canvas, cy + radius, h, cx, geo.light);
        draw_h_band(canvas, cx + radius, w, cy, geo.light);
        canvas.stroke_arc(cx + radius, cy + radius, radius, t, -1, -1);
        break;
    case 0x256E: // ╮ arms down + left
        draw_v_band(canvas, cy + radius, h, cx, geo.light);
        draw_h_band(canvas, 0, cx - radius, cy, geo.light);
        canvas.stroke_arc(cx - radius, cy + radius, radius, t, +1, -1);
        break;
    case 0x256F: // ╯ arms up + left
        draw_v_band(canvas, 0, cy - radius, cx, geo.light);
        draw_h_band(canvas, 0, cx - radius, cy, geo.light);
        canvas.stroke_arc(cx - radius, cy - radius, radius, t, +1, +1);
        break;
    case 0x2570: // ╰ arms up + right
        draw_v_band(canvas, 0, cy - radius, cx, geo.light);
        draw_h_band(canvas, cx + radius, w, cy, geo.light);
        canvas.stroke_arc(cx + radius, cy - radius, radius, t, -1, +1);
        break;
    default:
        break;
    }
}

void draw_box_drawing(Canvas& canvas, const BoxGeometry& geo, uint32_t cp)
{
    switch (cp)
    {
    case 0x2500: // ─
        draw_arms(canvas, geo, { 0, 0, 1, 1 });
        return;
    case 0x2501: // ━
        draw_arms(canvas, geo, { 0, 0, 2, 2 });
        return;
    case 0x2502: // │
        draw_arms(canvas, geo, { 1, 1, 0, 0 });
        return;
    case 0x2503: // ┃
        draw_arms(canvas, geo, { 2, 2, 0, 0 });
        return;
    case 0x2504: // ┄
        draw_dashed(canvas, geo, 3, false, 1);
        return;
    case 0x2505: // ┅
        draw_dashed(canvas, geo, 3, false, 2);
        return;
    case 0x2506: // ┆
        draw_dashed(canvas, geo, 3, true, 1);
        return;
    case 0x2507: // ┇
        draw_dashed(canvas, geo, 3, true, 2);
        return;
    case 0x2508: // ┈
        draw_dashed(canvas, geo, 4, false, 1);
        return;
    case 0x2509: // ┉
        draw_dashed(canvas, geo, 4, false, 2);
        return;
    case 0x250A: // ┊
        draw_dashed(canvas, geo, 4, true, 1);
        return;
    case 0x250B: // ┋
        draw_dashed(canvas, geo, 4, true, 2);
        return;
    case 0x254C: // ╌
        draw_dashed(canvas, geo, 2, false, 1);
        return;
    case 0x254D: // ╍
        draw_dashed(canvas, geo, 2, false, 2);
        return;
    case 0x254E: // ╎
        draw_dashed(canvas, geo, 2, true, 1);
        return;
    case 0x254F: // ╏
        draw_dashed(canvas, geo, 2, true, 2);
        return;
    case 0x2571: // ╱
        canvas.stroke_segment(0.0f, (float)geo.h, (float)geo.w, 0.0f, (float)geo.light);
        return;
    case 0x2572: // ╲
        canvas.stroke_segment(0.0f, 0.0f, (float)geo.w, (float)geo.h, (float)geo.light);
        return;
    case 0x2573: // ╳
        canvas.stroke_segment(0.0f, (float)geo.h, (float)geo.w, 0.0f, (float)geo.light);
        canvas.stroke_segment(0.0f, 0.0f, (float)geo.w, (float)geo.h, (float)geo.light);
        return;
    default:
        break;
    }

    if (cp >= 0x250C && cp <= 0x254B)
    {
        draw_arms(canvas, geo, kJunctionArms[cp - 0x250C]);
        return;
    }
    if (cp >= 0x2550 && cp <= 0x256C)
    {
        draw_double_lines(canvas, geo, cp);
        return;
    }
    if (cp >= 0x256D && cp <= 0x2570)
    {
        draw_rounded_corner(canvas, geo, cp);
        return;
    }
    if (cp >= 0x2574 && cp <= 0x257F)
    {
        draw_arms(canvas, geo, kHalfLineArms[cp - 0x2574]);
        return;
    }
}

void draw_block_element(Canvas& canvas, uint32_t cp)
{
    const float w = (float)canvas.w;
    const float h = (float)canvas.h;

    auto quadrants = [&](bool ul, bool ur, bool ll, bool lr) {
        if (ul)
            canvas.fill_rect(0, 0, w * 0.5f, h * 0.5f);
        if (ur)
            canvas.fill_rect(w * 0.5f, 0, w, h * 0.5f);
        if (ll)
            canvas.fill_rect(0, h * 0.5f, w * 0.5f, h);
        if (lr)
            canvas.fill_rect(w * 0.5f, h * 0.5f, w, h);
    };

    switch (cp)
    {
    case 0x2580: // ▀ upper half
        canvas.fill_rect(0, 0, w, h * 0.5f);
        return;
    case 0x2581: // ▁ .. 0x2587 ▇ lower eighths
    case 0x2582:
    case 0x2583:
    case 0x2584:
    case 0x2585:
    case 0x2586:
    case 0x2587:
        canvas.fill_rect(0, h * (8 - (float)(cp - 0x2580)) / 8.0f, w, h);
        return;
    case 0x2588: // █ full block
        canvas.fill_rect(0, 0, w, h);
        return;
    case 0x2589: // ▉ .. 0x258F ▏ left eighths
    case 0x258A:
    case 0x258B:
    case 0x258C:
    case 0x258D:
    case 0x258E:
    case 0x258F:
        canvas.fill_rect(0, 0, w * (8 - (float)(cp - 0x2588)) / 8.0f, h);
        return;
    case 0x2590: // ▐ right half
        canvas.fill_rect(w * 0.5f, 0, w, h);
        return;
    case 0x2591: // ░ light shade
        canvas.fill_rect(0, 0, w, h, 0.25f);
        return;
    case 0x2592: // ▒ medium shade
        canvas.fill_rect(0, 0, w, h, 0.5f);
        return;
    case 0x2593: // ▓ dark shade
        canvas.fill_rect(0, 0, w, h, 0.75f);
        return;
    case 0x2594: // ▔ upper eighth
        canvas.fill_rect(0, 0, w, h / 8.0f);
        return;
    case 0x2595: // ▕ right eighth
        canvas.fill_rect(w * 7.0f / 8.0f, 0, w, h);
        return;
    case 0x2596: // ▖ lower-left
        quadrants(false, false, true, false);
        return;
    case 0x2597: // ▗ lower-right
        quadrants(false, false, false, true);
        return;
    case 0x2598: // ▘ upper-left
        quadrants(true, false, false, false);
        return;
    case 0x2599: // ▙
        quadrants(true, false, true, true);
        return;
    case 0x259A: // ▚
        quadrants(true, false, false, true);
        return;
    case 0x259B: // ▛
        quadrants(true, true, true, false);
        return;
    case 0x259C: // ▜
        quadrants(true, true, false, true);
        return;
    case 0x259D: // ▝ upper-right
        quadrants(false, true, false, false);
        return;
    case 0x259E: // ▞
        quadrants(false, true, true, false);
        return;
    case 0x259F: // ▟
        quadrants(false, true, true, true);
        return;
    default:
        return;
    }
}

} // namespace

uint32_t synthesized_box_codepoint(std::string_view utf8_cluster)
{
    size_t offset = 0;
    uint32_t cp = 0;
    if (!utf8_decode_next(utf8_cluster, offset, cp))
        return 0;
    if (offset != utf8_cluster.size())
        return 0;
    if (cp >= 0x2500 && cp <= 0x259F)
        return cp;
    return 0;
}

std::vector<uint8_t> render_box_glyph(uint32_t cp, int w, int h)
{
    std::vector<uint8_t> alpha((size_t)std::max(w, 0) * std::max(h, 0), 0);
    if (w <= 0 || h <= 0)
        return alpha;

    Canvas canvas{ w, h, alpha };

    if (cp >= 0x2580 && cp <= 0x259F)
    {
        draw_block_element(canvas, cp);
        return alpha;
    }

    const int light = std::max(1, (int)std::lround(h / 16.0));
    BoxGeometry geo{ w, h, w * 0.5f, h * 0.5f, light, std::max(light * 2, light + 1) };
    draw_box_drawing(canvas, geo, cp);
    return alpha;
}

} // namespace draxul
