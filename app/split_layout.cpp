#include "split_layout.h"

namespace draxul
{

int SplitLayout::hit_test(int px, int py) const
{
    for (int i = 0; i < static_cast<int>(panes.size()); ++i)
    {
        const auto& desc = panes[static_cast<size_t>(i)].descriptor;
        if (px >= desc.pixel_x && px < desc.pixel_x + desc.pixel_width
            && py >= desc.pixel_y && py < desc.pixel_y + desc.pixel_height)
        {
            return i;
        }
    }
    return -1;
}

void SplitLayout::set_vertical_2(int pane0_id, int pane1_id, int total_pixel_w, int total_pixel_h)
{
    const int half_w = total_pixel_w / 2;

    panes.resize(2);
    panes[0].pane_id = pane0_id;
    panes[0].descriptor = { 0, 0, half_w, total_pixel_h };
    panes[0].focused = (focused_pane_index == 0);

    panes[1].pane_id = pane1_id;
    panes[1].descriptor = { half_w, 0, total_pixel_w - half_w, total_pixel_h };
    panes[1].focused = (focused_pane_index == 1);
}

void SplitLayout::set_single(int pane0_id, int total_pixel_w, int total_pixel_h)
{
    panes.resize(1);
    panes[0].pane_id = pane0_id;
    panes[0].descriptor = { 0, 0, total_pixel_w, total_pixel_h };
    panes[0].focused = true;
    focused_pane_index = 0;
}

const SplitPane* SplitLayout::focused() const
{
    if (focused_pane_index < 0 || focused_pane_index >= static_cast<int>(panes.size()))
        return nullptr;
    return &panes[static_cast<size_t>(focused_pane_index)];
}

} // namespace draxul
