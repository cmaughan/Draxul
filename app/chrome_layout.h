#pragma once

#include "app_shell_layout.h"
#include "chrome_pill.h"
#include "rename_editor.h"

#include <draxul/app_config_types.h>
#include <draxul/system_resource_monitor.h>
#include <optional>
#include <string>
#include <vector>

namespace draxul
{

constexpr int kChromeGridPadding = 4;
constexpr int kPaneContentInset = 8;
constexpr int kPaneFrameOuterMargin = 4;
constexpr int kPaneWindowEdgeExtraInset = 2;

enum class ChromeHitKind
{
    Space,
    Agent,
    Tab,
    PaneStatus,
};

struct ChromeHitRegion
{
    ChromeHitKind kind = ChromeHitKind::Tab;
    int stable_id = 0; // SpaceId, 1-based tab index, or LeafId
    ChromeRect rect{};
};

struct ChromeLabelCluster
{
    std::string text;
    int width = 1;
    Color fg{};
};

struct ChromeTabInput
{
    int tab_id = -1;
    std::string name;
    bool active = false;
};

struct ChromeSpaceInput
{
    int space_id = -1;
    std::string name;
    bool active = false;
};

struct ChromeAgentInput
{
    std::string instance_id;
    std::string display_name;
    std::string status_suffix;
    bool running = false;
    bool focused = false;
};

struct ChromePaneInput
{
    int pane_x = 0;
    int pane_y = 0;
    int pane_w = 0;
    int pane_h = 0;
    int index = 0;
    std::string text;
    bool focused = false;
    LeafId leaf = kInvalidLeaf;
    Color background{};
    int grid_rows = 0;
};

struct ChromeDivider
{
    ChromeRect rect{};
    SplitDirection direction = SplitDirection::Vertical;
};

struct ChromePaneFrameLayout
{
    LeafId leaf = kInvalidLeaf;
    ChromeRect outer{};
    ChromeRect rect{};
    ChromeRect content_tail{};
    Color content_background{};
    bool focused = false;
};

struct ChromeLayoutInput
{
    AppShellLayout shell_layout;
    int viewport_width = 0;
    int viewport_height = 0;
    int cell_width = 0;
    int cell_height = 0;
    int grid_padding = kChromeGridPadding;
    bool show_top_bar = false;
    bool show_status = false;
    ChromeTheme theme{};
    std::vector<ChromeTabInput> tabs;
    std::vector<ChromeSpaceInput> spaces;
    std::vector<ChromeAgentInput> agents;
    std::optional<SystemResourceSnapshot> resources;
    std::string weather_emoji;
    std::string weather_temperature;
    std::optional<std::pair<std::string, float>> chord;
    std::vector<ChromePaneInput> panes;
    std::vector<ChromeDivider> dividers;
    float focus_border = 3.0f;
    RenameSnapshot rename{};
};

struct ChromeTabLayout : ChromePillLayout
{
    int tab_id = -1;
    int tab_index = 0; // stable 1-based hit id
    int col_begin = 0;
    int col_end = 0;
    bool active = false;
    bool editing = false;
};

struct ChromeSpaceLayout : ChromePillLayout
{
    int space_id = -1;
    int space_index = 0;
    int row = 0;
    bool active = false;
};

struct ChromeAgentLayout : ChromePillLayout
{
    std::string instance_id;
    int agent_index = 0;
    int row = 0;
    bool running = false;
    bool focused = false;
};

struct ChromeRightPillLayout
{
    int col_begin = 0;
    int col_end = 0;
    int text_col = 0;
    Color bg{};
    Color accent_bg{};
    int accent_cols = 0;
    bool flat_right_edge = false;
    std::vector<ChromeLabelCluster> clusters;
    ChromeRect rect{};
    ChromeRect clip{};
    float accent_w = 0.0f;
};

struct ChromePaneLayout : ChromePillLayout
{
    LeafId leaf = kInvalidLeaf;
    int viewport_x = 0;
    int viewport_y = 0;
    int viewport_w = 0;
    int viewport_h = 0;
    bool focused = false;
    bool editing = false;
    bool number_only = false;
};

struct ChromeCaretLayout
{
    ChromeRect rect{};
};

struct ChromeLayoutOutput
{
    int content_x = 0;
    int bar_width = 0;
    int bar_height = 0;
    int grid_cols = 0;
    int cell_width = 0;
    int cell_height = 0;
    int grid_padding = kChromeGridPadding;
    int sidebar_width = 0;
    int sidebar_height = 0;
    int sidebar_cols = 0;
    int sidebar_rows = 0;
    int sidebar_agent_rows = 0;
    ChromeRect sidebar_rect{};
    ChromeRect sidebar_frame{};
    ChromeRect sidebar_spaces_header{};
    ChromeRect sidebar_spaces_rect{};
    ChromeRect sidebar_section_divider{};
    ChromeRect sidebar_agents_header{};
    ChromeRect sidebar_agents_rect{};
    ChromeRect sidebar_divider{};
    std::vector<ChromeSpaceLayout> spaces;
    std::vector<ChromeAgentLayout> agents;
    ChromeRect top_bar_clip{};
    std::vector<ChromeTabLayout> tabs;
    std::vector<ChromeRightPillLayout> right_pills;
    std::vector<ChromePaneLayout> panes;
    std::vector<ChromePaneFrameLayout> pane_frames;
    std::vector<ChromeDivider> dividers;
    std::optional<ChromeCaretLayout> tab_caret;
    std::optional<ChromeCaretLayout> pane_caret;
    std::vector<ChromeHitRegion> hit_regions;
    float focus_border = 3.0f;
    std::chrono::steady_clock::time_point edit_started_at{};
};

struct PaneStatusPillLayout
{
    bool visible = false;
    bool number_only = false;
    int pill_cols = 0;
    int text_cols = 0;
};

PaneStatusPillLayout pane_status_pill_layout(
    int pane_w_px, int cell_w_px, int number_cols, int status_text_cols, bool editing);
int pane_content_inset(float focus_border_width);
float pane_frame_line_inset(float focus_border_width);
int pane_content_edge_inset(float focus_border_width, bool window_edge);
float pane_frame_line_edge_inset(float focus_border_width, bool window_edge);

ChromeLayoutOutput compute_chrome_layout(const ChromeLayoutInput& input);
int hit_test_chrome(const ChromeLayoutOutput& layout, ChromeHitKind kind, int px, int py);

} // namespace draxul
