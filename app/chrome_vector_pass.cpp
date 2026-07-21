#include "chrome_vector_pass.h"

#include <cmath>
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
        for (const auto& status : layout.panes)
        {
            const auto& rect = status.rect;
            const float radius = rect.h * 0.5f;
            nvgBeginPath(vg);
            nvgRoundedRect(vg, rect.x, rect.y, rect.w, rect.h, radius);
            nvgFillColor(vg, nvg_color(status.editing ? theme.status_editing_bg : theme.status_bar_bg));
            nvgFill(vg);

            nvgSave(vg);
            nvgIntersectScissor(vg, status.clip.x, status.clip.y, status.clip.w, status.clip.h);
            nvgBeginPath(vg);
            nvgRoundedRect(vg, rect.x, rect.y, status.accent_w, rect.h, radius);
            nvgFillColor(vg, nvg_color(status.focused
                    ? theme.status_focused_accent_bg
                    : theme.status_inactive_accent_bg));
            nvgFill(vg);
            nvgRestore(vg);

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
            nvgRect(vg, 0.0f, 0.0f, static_cast<float>(layout.bar_width),
                static_cast<float>(layout.bar_height));
            nvgFillColor(vg, nvg_color(theme.tab_bar_bg));
            nvgFill(vg);
        }
        for (const auto& tab : layout.tabs)
        {
            const auto& rect = tab.rect;
            const float radius = rect.h * 0.5f;
            nvgBeginPath(vg);
            nvgRoundedRect(vg, rect.x, rect.y, rect.w, rect.h, radius);
            nvgFillColor(vg, nvg_color(tab.editing ? theme.tab_editing_bg : theme.tab_inactive_bg));
            nvgFill(vg);

            nvgSave(vg);
            nvgIntersectScissor(vg, tab.clip.x, tab.clip.y, tab.clip.w, tab.clip.h);
            nvgBeginPath(vg);
            nvgRoundedRect(vg, rect.x, rect.y, tab.accent_w, rect.h, radius);
            nvgFillColor(vg, nvg_color(tab.active ? theme.tab_active_bg : theme.tab_inactive_bg));
            nvgFill(vg);
            nvgRestore(vg);

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
            nvgBeginPath(vg);
            if (divider.direction == SplitDirection::Vertical)
            {
                const float center = rect.x + rect.w * 0.5f;
                nvgMoveTo(vg, center, rect.y);
                nvgLineTo(vg, center, rect.y + rect.h);
            }
            else
            {
                const float center = rect.y + rect.h * 0.5f;
                nvgMoveTo(vg, rect.x, center);
                nvgLineTo(vg, rect.x + rect.w, center);
            }
            nvgStrokeColor(vg, nvg_color(theme.divider));
            nvgStrokeWidth(vg, 1.0f);
            nvgStroke(vg);
        }

        if (layout.focus_rect)
        {
            const auto& focus = *layout.focus_rect;
            const float half = layout.focus_border * 0.5f;
            constexpr float divider_half = static_cast<float>(SplitTree::kDividerWidth) * 0.5f;
            const float pane_right = focus.x + focus.w;
            const float pane_bottom = focus.y + focus.h;
            bool right_divider = false;
            bool bottom_divider = false;
            for (const auto& divider : layout.dividers)
            {
                if (divider.direction == SplitDirection::Vertical
                    && std::abs(divider.rect.x - pane_right) < 1.0f)
                    right_divider = true;
                if (divider.direction == SplitDirection::Horizontal
                    && std::abs(divider.rect.y - pane_bottom) < 1.0f)
                    bottom_divider = true;
            }
            const float right = right_divider ? pane_right + divider_half : pane_right - half;
            const float bottom = bottom_divider ? pane_bottom + divider_half : pane_bottom - half;
            nvgStrokeColor(vg, nvg_color(theme.focus_border));
            nvgStrokeWidth(vg, layout.focus_border);
            nvgBeginPath(vg);
            nvgMoveTo(vg, right, focus.y);
            nvgLineTo(vg, right, bottom);
            nvgStroke(vg);
            nvgBeginPath(vg);
            nvgMoveTo(vg, focus.x, bottom);
            nvgLineTo(vg, right, bottom);
            nvgStroke(vg);
        }
    });

    RenderViewport viewport;
    viewport.width = viewport_width;
    viewport.height = viewport_height;
    frame.record_render_pass(*pass_, viewport);
}

} // namespace draxul
