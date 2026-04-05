#include <draxul/nanovg_demo_host.h>

#include <draxul/base_renderer.h>
#include <draxul/nanovg_pass.h>

#include "nanovg.h"

#include <cmath>

namespace draxul
{

bool NanoVGDemoHost::initialize(const HostContext& context, IHostCallbacks& callbacks)
{
    viewport_ = context.initial_viewport;
    callbacks_ = &callbacks;
    nanovg_pass_ = create_nanovg_pass();
    running_ = nanovg_pass_ != nullptr;
    return running_;
}

void NanoVGDemoHost::shutdown()
{
    nanovg_pass_.reset();
    running_ = false;
}

bool NanoVGDemoHost::is_running() const
{
    return running_;
}

void NanoVGDemoHost::set_viewport(const HostViewport& viewport)
{
    viewport_ = viewport;
}

HostRuntimeState NanoVGDemoHost::runtime_state() const
{
    HostRuntimeState state;
    state.content_ready = running_;
    return state;
}

HostDebugState NanoVGDemoHost::debug_state() const
{
    HostDebugState state;
    state.name = "NanoVG Demo";
    return state;
}

// ---------------------------------------------------------------------------
// Demo drawing — exercises convex fills, non-convex fills (stencil), strokes,
// linear/radial gradients, rounded rects, circles, and arcs.
// Modeled on the classic NanoVG demo from the original repository.
// ---------------------------------------------------------------------------

void NanoVGDemoHost::draw_demo(NVGcontext* vg, int w, int h)
{
    const float fw = static_cast<float>(w);
    const float fh = static_cast<float>(h);
    const float pad = 20.0f;

    // Background
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, fw, fh);
    nvgFillColor(vg, nvgRGBA(40, 44, 52, 255));
    nvgFill(vg);

    // --- Row 1: Rounded rectangles with gradients ---
    {
        const float y = pad;
        const float rw = 160.0f;
        const float rh = 80.0f;

        // Linear gradient fill
        NVGpaint bg = nvgLinearGradient(vg, pad, y, pad + rw, y + rh,
            nvgRGBA(64, 160, 255, 255), nvgRGBA(100, 60, 200, 255));
        nvgBeginPath(vg);
        nvgRoundedRect(vg, pad, y, rw, rh, 12.0f);
        nvgFillPaint(vg, bg);
        nvgFill(vg);

        // Solid fill
        nvgBeginPath(vg);
        nvgRoundedRect(vg, pad + rw + pad, y, rw, rh, 8.0f);
        nvgFillColor(vg, nvgRGBA(220, 80, 60, 230));
        nvgFill(vg);

        // Stroke only
        nvgBeginPath(vg);
        nvgRoundedRect(vg, pad + 2 * (rw + pad), y, rw, rh, 6.0f);
        nvgStrokeColor(vg, nvgRGBA(200, 200, 200, 255));
        nvgStrokeWidth(vg, 2.0f);
        nvgStroke(vg);

        // Radial gradient
        float cx = pad + 3 * (rw + pad) + rw / 2;
        float cy = y + rh / 2;
        NVGpaint radial = nvgRadialGradient(vg, cx, cy, 10.0f, rw / 2,
            nvgRGBA(255, 200, 50, 255), nvgRGBA(255, 50, 50, 0));
        nvgBeginPath(vg);
        nvgRoundedRect(vg, pad + 3 * (rw + pad), y, rw, rh, 12.0f);
        nvgFillPaint(vg, radial);
        nvgFill(vg);
    }

    // --- Row 2: Circles and arcs ---
    {
        const float y = 130.0f;
        const float r = 35.0f;
        const float spacing = 100.0f;

        // Solid circle
        nvgBeginPath(vg);
        nvgCircle(vg, pad + r, y + r, r);
        nvgFillColor(vg, nvgRGBA(80, 200, 120, 255));
        nvgFill(vg);

        // Circle with stroke
        nvgBeginPath(vg);
        nvgCircle(vg, pad + spacing + r, y + r, r);
        nvgFillColor(vg, nvgRGBA(60, 60, 80, 200));
        nvgFill(vg);
        nvgStrokeColor(vg, nvgRGBA(255, 180, 50, 255));
        nvgStrokeWidth(vg, 3.0f);
        nvgStroke(vg);

        // Arc (non-convex path — exercises stencil fill)
        nvgBeginPath(vg);
        nvgArc(vg, pad + 2 * spacing + r, y + r, r, 0.0f, NVG_PI * 1.5f, NVG_CW);
        nvgLineTo(vg, pad + 2 * spacing + r, y + r);
        nvgClosePath(vg);
        nvgFillColor(vg, nvgRGBA(200, 100, 200, 220));
        nvgFill(vg);

        // Ellipse
        nvgBeginPath(vg);
        nvgEllipse(vg, pad + 3 * spacing + r, y + r, r * 1.4f, r * 0.7f);
        NVGpaint ellipseGrad = nvgLinearGradient(vg,
            pad + 3 * spacing, y, pad + 3 * spacing + 2 * r, y + 2 * r,
            nvgRGBA(255, 100, 100, 255), nvgRGBA(100, 100, 255, 255));
        nvgFillPaint(vg, ellipseGrad);
        nvgFill(vg);
    }

    // --- Row 3: Non-convex polygon (star) — exercises stencil fill algorithm ---
    {
        const float cx = pad + 60.0f;
        const float cy = 270.0f;
        const float outerR = 50.0f;
        const float innerR = 20.0f;
        const int points = 5;

        nvgBeginPath(vg);
        for (int i = 0; i < points * 2; i++)
        {
            float angle = static_cast<float>(i) * NVG_PI / static_cast<float>(points) - NVG_PI / 2.0f;
            float radius = (i % 2 == 0) ? outerR : innerR;
            float px = cx + cosf(angle) * radius;
            float py = cy + sinf(angle) * radius;
            if (i == 0)
                nvgMoveTo(vg, px, py);
            else
                nvgLineTo(vg, px, py);
        }
        nvgClosePath(vg);
        nvgFillColor(vg, nvgRGBA(255, 210, 60, 240));
        nvgFill(vg);
        nvgStrokeColor(vg, nvgRGBA(180, 140, 20, 255));
        nvgStrokeWidth(vg, 2.0f);
        nvgStroke(vg);

        // Concentric ring (non-convex: outer circle with inner hole via winding)
        float ringCx = pad + 200.0f;
        float ringCy = 270.0f;
        nvgBeginPath(vg);
        nvgCircle(vg, ringCx, ringCy, 45.0f);
        nvgPathWinding(vg, NVG_SOLID);
        nvgCircle(vg, ringCx, ringCy, 25.0f);
        nvgPathWinding(vg, NVG_HOLE);
        NVGpaint ringGrad = nvgRadialGradient(vg, ringCx, ringCy, 20.0f, 50.0f,
            nvgRGBA(100, 255, 180, 255), nvgRGBA(50, 100, 200, 255));
        nvgFillPaint(vg, ringGrad);
        nvgFill(vg);
    }

    // --- Row 4: Lines with varying widths and caps ---
    {
        const float y = 350.0f;
        float x = pad;

        for (int i = 0; i < 8; i++)
        {
            float lw = 1.0f + static_cast<float>(i) * 1.5f;
            nvgBeginPath(vg);
            nvgMoveTo(vg, x, y);
            nvgLineTo(vg, x, y + 60.0f);
            nvgStrokeColor(vg, nvgRGBA(180, 180, 220, 255));
            nvgStrokeWidth(vg, lw);
            nvgLineCap(vg, NVG_ROUND);
            nvgStroke(vg);
            x += 25.0f;
        }

        // Bezier curve
        nvgBeginPath(vg);
        nvgMoveTo(vg, x + 20.0f, y);
        nvgBezierTo(vg, x + 60.0f, y - 30.0f, x + 100.0f, y + 90.0f, x + 140.0f, y + 60.0f);
        nvgStrokeColor(vg, nvgRGBA(255, 150, 80, 255));
        nvgStrokeWidth(vg, 3.0f);
        nvgStroke(vg);
    }

    // --- Row 5: Overlapping translucent shapes ---
    {
        const float y = 440.0f;
        const float size = 70.0f;

        NVGcolor colors[] = {
            nvgRGBA(255, 0, 0, 120),
            nvgRGBA(0, 255, 0, 120),
            nvgRGBA(0, 0, 255, 120),
        };

        for (int i = 0; i < 3; i++)
        {
            nvgBeginPath(vg);
            nvgCircle(vg, pad + 40.0f + static_cast<float>(i) * 35.0f,
                y + 35.0f + (i == 1 ? -15.0f : 0.0f), size / 2);
            nvgFillColor(vg, colors[i]);
            nvgFill(vg);
        }
    }

    // --- Label ---
    {
        nvgFontSize(vg, 18.0f);
        nvgFillColor(vg, nvgRGBA(200, 200, 200, 255));
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
        nvgText(vg, pad, fh - 30.0f, "NanoVG Render Test", nullptr);
    }
}

void NanoVGDemoHost::draw(IFrameContext& frame)
{
    if (!nanovg_pass_)
        return;

    nanovg_pass_->set_draw_callback(draw_demo);

    RenderViewport vp;
    vp.width = viewport_.pixel_size.x;
    vp.height = viewport_.pixel_size.y;
    frame.record_render_pass(*nanovg_pass_, vp);
    frame.flush_submit_chunk();
}

std::unique_ptr<IHost> create_nanovg_demo_host()
{
    return std::make_unique<NanoVGDemoHost>();
}

} // namespace draxul
