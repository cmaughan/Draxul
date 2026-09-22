#include <draxul/ui_panel.h>

int main()
{
    const auto layout = draxul::compute_panel_layout(
        800, 600, 8, 16, 1, false);
    return layout.grid_size.x == 99 && layout.grid_size.y == 37 ? 0 : 1;
}
