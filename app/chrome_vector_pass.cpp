#include "chrome_vector_pass.h"

#include <algorithm>
#include <nanovg.h>

namespace draxul
{
namespace
{
NVGcolor nvg_color(const Color& color)
{
    return nvgRGBAf(color.r, color.g, color.b, color.a);
}

bool caret_visible(std::chrono::steady_clock::time_point started_at)
{
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started_at)
                             .count();
    return (elapsed / 500) % 2 == 0;
}

void draw_caret(NVGcontext* vg, const std::optional<ChromeCaretLayout>& caret,
    std::chrono::steady_clock::time_point started_at, const Color& color)
{
    if (!caret || !caret_visible(started_at))
        return;
    const auto& rect = caret->rect;
    nvgBeginPath(vg);
    nvgRect(vg, rect.x, rect.y, rect.w, rect.h);
    nvgFillColor(vg, nvg_color(color));
    nvgFill(vg);
}

void draw_segmented_pill(NVGcontext* vg, const ChromeRect& rect,
    const ChromeRect& clip, float accent_w, const Color& body_bg,
    const Color& accent_bg)
{
    const float radius = rect.h * 0.5f;
    nvgBeginPath(vg);
    nvgRoundedRect(vg, rect.x, rect.y, rect.w, rect.h, radius);
    nvgFillColor(vg, nvg_color(body_bg));
    nvgFill(vg);

    nvgSave(vg);
    nvgIntersectScissor(vg, clip.x, clip.y, clip.w, clip.h);
    nvgBeginPath(vg);
    nvgRoundedRect(vg, rect.x, rect.y, accent_w, rect.h, radius);
    nvgFillColor(vg, nvg_color(accent_bg));
    nvgFill(vg);
    nvgRestore(vg);
}
} // namespace

bool ChromeVectorPass::initialize()
{
    pass_ = create_nanovg_pass();
    return pass_ != nullptr;
}

void ChromeVectorPass::shutdown()
{
    pass_.reset();
}

bool ChromeVectorPass::available() const
{
    return pass_ != nullptr;
}

void ChromeVectorPass::record(IFrameContext& frame, const ChromeLayoutOutput& layout,
    const ChromeTheme& theme, int viewport_width, int viewport_height)
{
    if (!pass_)
        return;
    pass_->set_draw_callback([layout, theme](NVGcontext* vg, int, int) {
        if (layout.sidebar_width > 0)
        {
            nvgBeginPath(vg);
            nvgRect(vg, layout.sidebar_rect.x, layout.sidebar_rect.y,
                layout.sidebar_rect.w, layout.sidebar_rect.h);
            nvgFillColor(vg, nvg_color(theme.tab_bar_bg));
            nvgFill(vg);

            for (const auto& space : layout.spaces)
            {
                draw_segmented_pill(vg, space.rect, space.clip, space.accent_w,
                    space.palette.body_bg, space.palette.accent_bg);
            }

            if (layout.sidebar_section_divider.h > 0.0f)
            {
                nvgBeginPath(vg);
                nvgRect(vg,
                    layout.sidebar_section_divider.x,
                    layout.sidebar_section_divider.y,
                    layout.sidebar_section_divider.w,
                    layout.sidebar_section_divider.h);
                nvgFillColor(vg, nvg_color(theme.divider));
                nvgFill(vg);
            }

            constexpr float shell_frame_inset = 2.0f;
            nvgBeginPath(vg);
            nvgRect(vg,
                layout.sidebar_rect.x + shell_frame_inset,
                layout.sidebar_rect.y + shell_frame_inset,
                std::max(0.0f, layout.sidebar_rect.w - shell_frame_inset * 2.0f),
                std::max(0.0f, layout.sidebar_rect.h - shell_frame_inset * 2.0f));
            nvgStrokeColor(vg, nvg_color(theme.tab_inactive_bg));
            nvgStrokeWidth(vg, 1.0f);
            nvgStroke(vg);

            if (layout.sidebar_divider.w > 0)
            {
                nvgBeginPath(vg);
                nvgRect(vg, layout.sidebar_divider.x, layout.sidebar_divider.y,
                    layout.sidebar_divider.w, layout.sidebar_divider.h);
                nvgFillColor(vg, nvg_color(theme.divider));
                nvgFill(vg);
            }
        }

        for (const auto& pane : layout.pane_frames)
        {
            nvgBeginPath(vg);
            nvgRect(vg, pane.outer.x, pane.outer.y, pane.outer.w, pane.outer.h);
            nvgFillColor(vg, nvg_color(theme.tab_bar_bg));
            nvgFill(vg);

            if (pane.content_tail.w > 0.0f && pane.content_tail.h > 0.0f)
            {
                nvgBeginPath(vg);
                nvgRect(vg, pane.content_tail.x, pane.content_tail.y,
                    pane.content_tail.w, pane.content_tail.h);
                nvgFillColor(vg, nvg_color(pane.content_background));
                nvgFill(vg);
            }

            nvgBeginPath(vg);
            nvgRect(vg, pane.rect.x, pane.rect.y, pane.rect.w, pane.rect.h);
            nvgStrokeColor(vg, nvg_color(
                pane.focused ? theme.focus_border : theme.tab_inactive_bg));
            nvgStrokeWidth(vg, pane.focused ? layout.focus_border : 1.0f);
            nvgStroke(vg);
        }

        for (const auto& status : layout.panes)
        {
            const auto& rect = status.rect;
            const float radius = rect.h * 0.5f;
            draw_segmented_pill(vg, rect, status.clip, status.accent_w,
                status.palette.body_bg, status.palette.accent_bg);

            if (status.editing)
            {
                nvgBeginPath(vg);
                nvgRoundedRect(vg, rect.x, rect.y, rect.w, rect.h, radius);
                nvgStrokeColor(vg, nvg_color(theme.editing_outline));
                nvgStrokeWidth(vg, 1.5f);
                nvgStroke(vg);
            }
        }
        draw_caret(vg, layout.pane_caret, layout.edit_started_at, theme.editing_outline);

        if (layout.bar_height > 0)
        {
            nvgBeginPath(vg);
            nvgRect(vg, layout.top_bar_clip.x, layout.top_bar_clip.y,
                layout.top_bar_clip.w, layout.top_bar_clip.h);
            nvgFillColor(vg, nvg_color(theme.tab_bar_bg));
            nvgFill(vg);
        }
        for (const auto& tab : layout.tabs)
        {
            const auto& rect = tab.rect;
            const float radius = rect.h * 0.5f;
            draw_segmented_pill(vg, rect, tab.clip, tab.accent_w,
                tab.palette.body_bg, tab.palette.accent_bg);

            if (tab.editing)
            {
                nvgBeginPath(vg);
                nvgRoundedRect(vg, rect.x, rect.y, rect.w, rect.h, radius);
                nvgStrokeColor(vg, nvg_color(theme.editing_outline));
                nvgStrokeWidth(vg, 1.5f);
                nvgStroke(vg);
            }
        }
        for (const auto& pill : layout.right_pills)
        {
            const auto& rect = pill.rect;
            const float radius = rect.h * 0.5f;
            nvgBeginPath(vg);
            if (pill.flat_right_edge)
                nvgRoundedRectVarying(vg, rect.x, rect.y, rect.w, rect.h, radius, 0.0f, 0.0f, radius);
            else
                nvgRoundedRect(vg, rect.x, rect.y, rect.w, rect.h, radius);
            nvgFillColor(vg, nvg_color(pill.bg));
            nvgFill(vg);
        }
        draw_caret(vg, layout.tab_caret, layout.edit_started_at, theme.editing_outline);

        for (const auto& divider : layout.dividers)
        {
            const auto& rect = divider.rect;
            if (rect.w <= 0.0f || rect.h <= 0.0f)
                continue;
            nvgBeginPath(vg);
            nvgRect(vg, rect.x, rect.y, rect.w, rect.h);
            nvgFillColor(vg, nvg_color(theme.tab_bar_bg));
            nvgFill(vg);
        }

    });

    RenderViewport viewport;
    viewport.width = viewport_width;
    viewport.height = viewport_height;
    frame.record_render_pass(*pass_, viewport);
}

} // namespace draxul
