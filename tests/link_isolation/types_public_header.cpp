#include <draxul/types.h>
#include <draxul/pane_descriptor.h>
#include <draxul/system_resource_snapshot.h>
#include <draxul/unicode.h>

int main()
{
    const auto clusters = draxul::display_clusters("A\xE7\x95\x8C");
    const draxul::PaneDescriptor pane{ { 0, 0 }, { 80, 24 } };
    const draxul::SystemResourceSnapshot resources{ 25, 50 };
    return clusters.size() == 2 && draxul::display_cell_width("A\xE7\x95\x8C") == 3
            && pane.pixel_size.x == 80 && resources.available()
        ? 0
        : 1;
}
