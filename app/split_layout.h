#pragma once
#include <draxul/pane_descriptor.h>
#include <vector>

namespace draxul
{

struct SplitPane
{
    int pane_id = 0;
    PaneDescriptor descriptor;
    bool focused = false;
};

struct SplitLayout
{
    std::vector<SplitPane> panes;
    int focused_pane_index = 0;

    // Returns pane index containing (px, py), or -1 if none.
    int hit_test(int px, int py) const;

    // Recompute two equal vertical halves from total window size.
    void set_vertical_2(int pane0_id, int pane1_id, int total_pixel_w, int total_pixel_h);

    // Single-pane layout (initial state).
    void set_single(int pane0_id, int total_pixel_w, int total_pixel_h);

    const SplitPane* focused() const;
};

} // namespace draxul
