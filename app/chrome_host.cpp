#include "chrome_host.h"

#include <nanovg.h>

namespace draxul
{

ChromeHost::ChromeHost(const SplitTree& tree)
    : tree_(tree)
{
}

bool ChromeHost::initialize(const HostContext& context, IHostCallbacks& /*callbacks*/)
{
    viewport_ = context.initial_viewport;
    nanovg_pass_ = create_nanovg_pass();
    running_ = nanovg_pass_ != nullptr;
    return running_;
}

void ChromeHost::shutdown()
{
    nanovg_pass_.reset();
    running_ = false;
}

bool ChromeHost::is_running() const
{
    return running_;
}

void ChromeHost::set_viewport(const HostViewport& viewport)
{
    viewport_ = viewport;
}

void ChromeHost::draw(IFrameContext& frame)
{
    if (!nanovg_pass_ || tree_.leaf_count() < 2)
        return;

    // Collect divider rects from the split tree.
    struct Divider
    {
        float x, y, w, h;
        SplitDirection dir;
    };
    std::vector<Divider> dividers;
    tree_.for_each_divider([&](const SplitTree::DividerRect& r) {
        dividers.push_back({ static_cast<float>(r.x), static_cast<float>(r.y),
            static_cast<float>(r.w), static_cast<float>(r.h), r.direction });
    });

    if (dividers.empty())
        return;

    // Focused pane border rect (in pixels).
    struct FocusRect
    {
        float x, y, w, h;
    };
    std::optional<FocusRect> focus_rect;
    const LeafId focused = tree_.focused();
    if (focused != kInvalidLeaf)
    {
        const auto desc = tree_.descriptor_for(focused);
        if (desc.pixel_size.x > 0 && desc.pixel_size.y > 0)
        {
            focus_rect = FocusRect{
                static_cast<float>(desc.pixel_pos.x),
                static_cast<float>(desc.pixel_pos.y),
                static_cast<float>(desc.pixel_size.x),
                static_cast<float>(desc.pixel_size.y)
            };
        }
    }

    nanovg_pass_->set_draw_callback(
        [dividers = std::move(dividers), focus_rect](NVGcontext* vg, int /*w*/, int /*h*/) {
            // Divider lines
            for (const auto& d : dividers)
            {
                nvgBeginPath(vg);
                if (d.dir == SplitDirection::Vertical)
                {
                    float cx = d.x + d.w * 0.5f;
                    nvgMoveTo(vg, cx, d.y);
                    nvgLineTo(vg, cx, d.y + d.h);
                }
                else
                {
                    float cy = d.y + d.h * 0.5f;
                    nvgMoveTo(vg, d.x, cy);
                    nvgLineTo(vg, d.x + d.w, cy);
                }
                nvgStrokeColor(vg, nvgRGBA(120, 120, 140, 220));
                nvgStrokeWidth(vg, 1.0f);
                nvgStroke(vg);
            }

            // Focus indicator — muted burgundy on right and bottom edges only.
            // At divider boundaries, draw on the divider centre line.
            // At window edges, inset by half the stroke so it stays visible.
            if (focus_rect)
            {
                constexpr float border = 2.0f;
                constexpr float half = border * 0.5f;
                constexpr float div_half = static_cast<float>(SplitTree::kDividerWidth) * 0.5f;

                const float pane_right = focus_rect->x + focus_rect->w;
                const float pane_bottom = focus_rect->y + focus_rect->h;

                // Check whether a divider sits on the right / bottom edge of
                // the focused pane.  If so, draw on the divider centre line;
                // if not (window edge), inset so the stroke stays visible.
                bool has_right_divider = false;
                bool has_bottom_divider = false;
                for (const auto& d : dividers)
                {
                    if (d.dir == SplitDirection::Vertical
                        && std::abs(d.x - pane_right) < 1.0f)
                        has_right_divider = true;
                    if (d.dir == SplitDirection::Horizontal
                        && std::abs(d.y - pane_bottom) < 1.0f)
                        has_bottom_divider = true;
                }
                const float right = has_right_divider ? (pane_right + div_half) : (pane_right - half);
                const float bottom = has_bottom_divider ? (pane_bottom + div_half) : (pane_bottom - half);

                nvgStrokeColor(vg, nvgRGBA(140, 50, 55, 200));
                nvgStrokeWidth(vg, border);

                // Right edge
                nvgBeginPath(vg);
                nvgMoveTo(vg, right, focus_rect->y);
                nvgLineTo(vg, right, bottom);
                nvgStroke(vg);

                // Bottom edge
                nvgBeginPath(vg);
                nvgMoveTo(vg, focus_rect->x, bottom);
                nvgLineTo(vg, right, bottom);
                nvgStroke(vg);
            }
        });

    RenderViewport vp;
    vp.width = viewport_.pixel_size.x;
    vp.height = viewport_.pixel_size.y;
    frame.record_render_pass(*nanovg_pass_, vp);
}

} // namespace draxul
