#include <draxul/scoreview/score_host.h>

#include <draxul/base_renderer.h>
#include <draxul/host_registry.h>

#include "nanovg.h"

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace draxul
{
namespace scoreview
{

namespace
{

// Page and engraving proportions for the placeholder scene. Staff-relative
// thicknesses follow SMuFL engravingDefaults so the placeholder already reads
// like engraved music; absolute sizes are generous for a single system.
constexpr float PAGE_ASPECT = 0.7071f; // A4 portrait width/height
constexpr float STAFF_HEIGHT_FRAC = 0.045f; // staff height / page height
constexpr float STAFF_LINE_THICKNESS_SP = 0.13f;
constexpr float BARLINE_THIN_SP = 0.16f;
constexpr float BARLINE_THICK_SP = 0.5f;
constexpr float BARLINE_SEPARATION_SP = 0.4f;
constexpr int PLACEHOLDER_MEASURES = 4;

const NVGcolor INK = { { { 0.12f, 0.11f, 0.10f, 1.0f } } };
const NVGcolor PAGE_WHITE = { { { 0.988f, 0.984f, 0.972f, 1.0f } } };

void fill_vertical_bar(NVGcontext* vg, float x_center, float top, float bottom, float width)
{
    nvgBeginPath(vg);
    nvgRect(vg, x_center - width * 0.5f, top, width, bottom - top);
    nvgFillColor(vg, INK);
    nvgFill(vg);
}

// Curly brace joining the two staves, built from four cubics pinched at the
// tips and waist. Hand-tuned placeholder — Phase 3 replaces it with the
// Bravura brace glyph.
void fill_brace(NVGcontext* vg, float right_x, float top, float bottom, float sp)
{
    const float mid = (top + bottom) * 0.5f;
    const float half = mid - top;
    const float lobe = right_x - 2.4f * sp; // leftmost extent of the lobes
    const float inner = right_x - 1.5f * sp; // return-edge extent (sets stroke weight)
    const float waist_x = right_x - 0.5f * sp;

    nvgBeginPath(vg);
    nvgMoveTo(vg, right_x, top);
    nvgBezierTo(vg, lobe, top + half * 0.30f, lobe, mid - half * 0.12f, waist_x, mid);
    nvgBezierTo(vg, lobe, mid + half * 0.12f, lobe, bottom - half * 0.30f, right_x, bottom);
    nvgBezierTo(vg, inner, bottom - half * 0.30f, inner, mid + half * 0.12f, waist_x, mid);
    nvgBezierTo(vg, inner, mid - half * 0.12f, inner, top + half * 0.30f, right_x, top);
    nvgClosePath(vg);
    nvgFillColor(vg, INK);
    nvgFill(vg);
}

void draw_placeholder(NVGcontext* vg, int width, int height, float pixel_scale)
{
    const float fw = static_cast<float>(width);
    const float fh = static_cast<float>(height);
    const float outer_margin = 24.0f * pixel_scale;
    const float avail_w = fw - 2.0f * outer_margin;
    const float avail_h = fh - 2.0f * outer_margin;
    if (avail_w < 64.0f || avail_h < 64.0f)
        return;

    // Fit an A4 portrait page into the viewport.
    float page_h = avail_h;
    float page_w = page_h * PAGE_ASPECT;
    if (page_w > avail_w)
    {
        page_w = avail_w;
        page_h = page_w / PAGE_ASPECT;
    }
    const float px = (fw - page_w) * 0.5f;
    const float py = (fh - page_h) * 0.5f;

    // Drop shadow, then the page itself.
    const float corner = 2.0f * pixel_scale;
    NVGpaint shadow = nvgBoxGradient(vg, px, py + 3.0f * pixel_scale, page_w, page_h,
        corner * 2.0f, 14.0f * pixel_scale, nvgRGBA(0, 0, 0, 80), nvgRGBA(0, 0, 0, 0));
    nvgBeginPath(vg);
    nvgRect(vg, px - 24.0f * pixel_scale, py - 24.0f * pixel_scale,
        page_w + 48.0f * pixel_scale, page_h + 48.0f * pixel_scale);
    nvgRoundedRect(vg, px, py, page_w, page_h, corner);
    nvgPathWinding(vg, NVG_HOLE);
    nvgFillPaint(vg, shadow);
    nvgFill(vg);

    nvgBeginPath(vg);
    nvgRoundedRect(vg, px, py, page_w, page_h, corner);
    nvgFillColor(vg, PAGE_WHITE);
    nvgFill(vg);

    // Grand staff: two five-line staves at engraving proportions.
    const float staff_h = page_h * STAFF_HEIGHT_FRAC;
    const float sp = staff_h / 4.0f;
    const float line_w = std::max(1.0f, STAFF_LINE_THICKNESS_SP * sp);
    const float left = px + page_w * 0.10f;
    const float right = px + page_w * 0.90f;
    const float upper_top = py + page_h * 0.18f;
    const float lower_top = upper_top + staff_h + 1.5f * staff_h;
    const float system_bottom = lower_top + staff_h;

    for (int staff = 0; staff < 2; ++staff)
    {
        const float top = staff == 0 ? upper_top : lower_top;
        for (int i = 0; i < 5; ++i)
        {
            const float y = top + static_cast<float>(i) * sp;
            nvgBeginPath(vg);
            nvgMoveTo(vg, left, y);
            nvgLineTo(vg, right, y);
            nvgStrokeColor(vg, INK);
            nvgStrokeWidth(vg, line_w);
            nvgStroke(vg);
        }
    }

    // Barlines: system barline + measure divisions + final thin/thick pair.
    const float thin_w = std::max(1.0f, BARLINE_THIN_SP * sp);
    const float thick_w = BARLINE_THICK_SP * sp;
    for (int i = 0; i < PLACEHOLDER_MEASURES; ++i)
    {
        const float x = left + (right - left) * static_cast<float>(i) / PLACEHOLDER_MEASURES;
        fill_vertical_bar(vg, x, upper_top, system_bottom, thin_w);
    }
    const float final_thin_x = right - thick_w - BARLINE_SEPARATION_SP * sp - thin_w * 0.5f;
    fill_vertical_bar(vg, final_thin_x, upper_top, system_bottom, thin_w);
    fill_vertical_bar(vg, right - thick_w * 0.5f, upper_top, system_bottom, thick_w);

    fill_brace(vg, left - 0.6f * sp, upper_top, system_bottom, sp);
}

} // namespace

bool ScoreHost::initialize(const HostContext& context, IHostCallbacks& callbacks)
{
    viewport_ = context.initial_viewport;
    callbacks_ = &callbacks;
    source_path_ = context.launch_options.source_path;
    nanovg_pass_ = create_nanovg_pass();
    running_ = nanovg_pass_ != nullptr;
    return running_;
}

void ScoreHost::shutdown()
{
    nanovg_pass_.reset();
    running_ = false;
}

bool ScoreHost::is_running() const
{
    return running_;
}

void ScoreHost::set_viewport(const HostViewport& viewport)
{
    viewport_ = viewport;
}

void ScoreHost::draw(IFrameContext& frame)
{
    if (!nanovg_pass_)
        return;

    const float pixel_scale = viewport_.pixel_scale > 0.0f ? viewport_.pixel_scale : 1.0f;
    nanovg_pass_->set_draw_callback([pixel_scale](NVGcontext* vg, int w, int h) {
        draw_placeholder(vg, w, h, pixel_scale);
    });

    RenderViewport vp;
    vp.width = viewport_.pixel_size.x;
    vp.height = viewport_.pixel_size.y;
    frame.record_render_pass(*nanovg_pass_, vp);
    frame.flush_submit_chunk();
}

void ScoreHost::request_close()
{
    running_ = false;
}

std::string ScoreHost::status_text() const
{
    if (source_path_.empty())
        return "score: no --source (phase 0 placeholder)";
    return "score: " + std::filesystem::path(source_path_).filename().string() + " (phase 0 placeholder — import pending)";
}

Color ScoreHost::default_background() const
{
    return Color{ 56, 58, 62, 255 };
}

HostRuntimeState ScoreHost::runtime_state() const
{
    HostRuntimeState state;
    state.content_ready = running_;
    return state;
}

HostDebugState ScoreHost::debug_state() const
{
    HostDebugState state;
    state.name = "ScoreView";
    return state;
}

std::unique_ptr<IHost> create_score_host()
{
    return std::make_unique<ScoreHost>();
}

void register_score_host_provider(HostProviderRegistry& registry)
{
    registry.register_provider(HostKind::Score, &create_score_host);
}

} // namespace scoreview
} // namespace draxul
