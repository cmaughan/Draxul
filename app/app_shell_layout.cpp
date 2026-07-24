#include "app_shell_layout.h"

#include <algorithm>

namespace draxul
{

bool contains(const AppShellRect& rect, int x, int y)
{
    return rect.w > 0 && rect.h > 0
        && x >= rect.x && x < rect.x + rect.w
        && y >= rect.y && y < rect.y + rect.h;
}

AppShellLayout compute_app_shell_layout(const AppShellLayoutInput& input)
{
    AppShellLayout out;
    const int window_width = std::max(0, input.window_width);
    const int window_height = std::max(0, input.window_height);
    const int terminal_height = std::clamp(input.terminal_height, 0, window_height);

    out.window = { 0, 0, window_width, window_height };
    out.work_area = { 0, 0, window_width, terminal_height };
    out.diagnostics = { 0, terminal_height, window_width, window_height - terminal_height };
    out.chrome_visible = !input.zoomed;

    if (input.zoomed)
    {
        out.content = out.work_area;
        out.pane_root = out.work_area;
        return out;
    }

    int content_x = 0;
    if (input.show_sidebar && input.cell_width > 0 && terminal_height > 0)
    {
        const int preferred_columns = std::clamp(input.preferred_sidebar_columns,
            kMinSpaceSidebarColumns, kMaxSpaceSidebarColumns);
        const int minimum_content_width = kMinContentColumns * input.cell_width;
        const int available_sidebar_width = std::max(
            0, window_width - minimum_content_width - kAppShellDividerWidth);
        const int sidebar_width = std::min(
            preferred_columns * input.cell_width,
            available_sidebar_width / input.cell_width * input.cell_width);
        if (sidebar_width >= input.cell_width)
        {
            out.sidebar_visible = true;
            out.effective_sidebar_columns = sidebar_width / input.cell_width;
            out.sidebar = { 0, 0, sidebar_width, terminal_height };
            const int section_divider_height = std::min(
                kAppShellSectionDividerHeight, terminal_height);
            const int minimum_agents_height = std::min(
                kMinSidebarAgentRows * std::max(0, input.cell_height),
                std::max(0, terminal_height - section_divider_height));
            const int maximum_divider_y = std::max(
                0, terminal_height - section_divider_height - minimum_agents_height);
            const int desired_divider_y = std::max(0,
                (std::max(0, input.space_count) + kSidebarFirstSpaceRow)
                        * input.cell_height
                    + input.cell_height / 2);
            const int section_divider_y = std::min(
                desired_divider_y, maximum_divider_y);
            out.sidebar_spaces = { 0, 0, sidebar_width, section_divider_y };
            out.sidebar_section_divider = {
                0, section_divider_y, sidebar_width, section_divider_height
            };
            out.sidebar_agents = {
                0,
                section_divider_y + section_divider_height,
                sidebar_width,
                std::max(0, terminal_height - section_divider_y - section_divider_height)
            };
            out.sidebar_divider = {
                sidebar_width, 0, kAppShellDividerWidth, terminal_height
            };
            content_x = sidebar_width + kAppShellDividerWidth;
        }
    }

    out.content = { content_x, 0, std::max(0, window_width - content_x), terminal_height };
    const int tab_bar_height = input.show_tab_bar && input.cell_height > 0
        ? std::min(terminal_height, input.cell_height + 2)
        : 0;
    out.tab_bar = { content_x, 0, out.content.w, tab_bar_height };
    out.pane_root = {
        content_x, tab_bar_height, out.content.w, std::max(0, terminal_height - tab_bar_height)
    };
    return out;
}

} // namespace draxul
