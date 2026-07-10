#pragma once

#include "workspace.h"

#include <chrono>
#include <draxul/app_config_types.h>
#include <draxul/base_renderer.h>
#include <draxul/host.h>
#include <draxul/host_kind.h>
#include <draxul/nanovg_pass.h>
#include <draxul/renderer.h>
#include <draxul/system_resource_monitor.h>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

struct NVGcontext;

namespace draxul
{

// Layout for one pane's bottom status pill. Both the NanoVG pill/accent pass
// and the grid text pass derive their geometry from pane_status_pill_layout()
// so they can never disagree. Narrow panes degrade in tiers:
// full text → truncated text → number-only pill → hidden.
struct PaneStatusPillLayout
{
    bool visible = false;
    bool number_only = false; // label is just "N:" and the accent spans the pill
    int pill_cols = 0; // total column span including padding cells
    int text_cols = 0; // columns granted to the status-text portion
};

// number_cols = digits in the pane index; status_text_cols = display columns
// of the status text (measured in cells, not bytes).
PaneStatusPillLayout pane_status_pill_layout(
    int pane_w_px, int cell_w_px, int number_cols, int status_text_cols, bool editing);

// ChromeHost is the central layout manager. It owns one or more Workspaces
// (each with its own SplitTree + hosts) and draws window chrome (pane dividers,
// focus indicator, future tab bar) using NanoVG.
class ChromeHost final : public IHost
{
public:
    // Shared dependencies passed to every workspace's HostManager.
    struct Deps
    {
        const AppOptions* options = nullptr;
        AppConfig* config = nullptr;
        ConfigDocument* config_document = nullptr;
        IWindow* window = nullptr;
        IGridRenderer* grid_renderer = nullptr;
        IImGuiHost* imgui_host = nullptr;
        TextService* text_service = nullptr;
        const float* display_ppi = nullptr;
        std::weak_ptr<void> owner_lifetime;
        std::function<HostViewport(const PaneDescriptor&)> compute_viewport;

        // Read-only workspace info for tab bar / divider rendering (owned by App).
        const std::vector<std::unique_ptr<Workspace>>* workspaces = nullptr;
        const int* active_workspace_id = nullptr;
        const SystemResourceSnapshot* system_resource_snapshot = nullptr;
        std::function<std::optional<std::pair<std::string, float>>()> chord_indicator = nullptr;
        // Weather callbacks — return emoji (for example "\u2600\uFE0F") and
        // temperature ("18°C") separately
        // for split-styled rendering. Both empty = no weather pill shown.
        std::function<std::string()> weather_emoji;
        std::function<std::string()> weather_temperature;

        // Apply a user-typed name to a workspace tab. Owner (App) sets
        // workspace.name and marks workspace.name_user_set so subsequent OSC 7
        // updates don't overwrite the user's choice.
        std::function<void(int workspace_id, std::string name)> set_workspace_name;
        // Apply a user-typed name to a pane (per-leaf override). Owner (App)
        // forwards this to HostManager::set_pane_name. Empty name clears the
        // override and reverts to host->status_text().
        std::function<void(LeafId leaf, std::string name)> set_pane_name;
        // Look up the existing user override for a pane. Returns empty string
        // when no override is set. Used to seed the rename buffer.
        std::function<std::string(LeafId leaf)> get_pane_name;
        // Request the host to schedule another frame even when no input
        // arrived (used to drive the rename caret blink).
        std::function<void()> request_frame;
    };

    explicit ChromeHost(Deps deps);

    // IHost overrides
    bool initialize(const HostContext& context, IHostCallbacks& callbacks) override;
    void shutdown() override;
    bool is_running() const override;
    std::string init_error() const override
    {
        return {};
    }

    void set_viewport(const HostViewport& viewport) override;
    void pump() override {}
    void draw(IFrameContext& frame) override;
    std::optional<std::chrono::steady_clock::time_point> next_deadline() const override;

    bool dispatch_action(std::string_view /*action*/) override
    {
        return false;
    }
    void request_close() override {}
    Color default_background() const override
    {
        return { 0, 0, 0, 0 };
    }
    HostRuntimeState runtime_state() const override
    {
        return { true };
    }
    HostDebugState debug_state() const override
    {
        return { "chrome" };
    }

    // Tab bar height in pixels. Remains visible even with a single workspace.
    int tab_bar_height() const;

    // Hit-test a point (physical pixels) against the tab bar.
    // Returns the 1-based tab index if hit, or 0 if not in the tab bar.
    int hit_test_tab(int px, int py) const;

    // ----- Inline tab/pane rename (WI 128) -----------------------------
    // A single edit session can target either a workspace tab or a pane
    // status pill. Only one target is active at a time; starting a new edit
    // commits any in-progress one.
    //
    // Workspace target:
    void begin_tab_rename(int tab_index);
    // Begin editing the tab corresponding to a workspace_id. Same semantics
    // as begin_tab_rename(int). Used by the rename_tab command palette
    // action which knows the active workspace id, not its 1-based index.
    void begin_tab_rename_by_id(int workspace_id);
    bool is_editing_tab() const;
    int editing_workspace_id() const;
    // Pane target:
    void begin_pane_rename(LeafId leaf);
    bool is_editing_pane() const;
    LeafId editing_leaf_id() const;
    // True while any rename session (tab or pane) is in progress. Used by
    // InputDispatcher to route key/text events to the rename layer.
    bool is_editing() const;
    // Commit the current edit buffer (tab or pane) and exit edit mode.
    // No-op when not editing. Empty buffers do not overwrite the existing
    // name — they just exit edit mode.
    void commit_tab_rename();
    void cancel_tab_rename();
    // Forward a text input event to the active rename buffer. Returns true
    // if the event was consumed.
    bool on_rename_text_input(const std::string& utf8);
    // Forward a key event to the active rename buffer. Handles Enter /
    // Escape / Backspace / Delete / Left / Right / Home / End. Returns true
    // if the event was consumed.
    bool on_rename_key(int sdl_keycode);

    // Hit-test a point (physical pixels) against the per-pane status pills.
    // Returns the LeafId of the pane whose pill contains the point, or
    // kInvalidLeaf if none. Only valid after the first draw() call —
    // ChromeHost caches the pill rects from the most recent frame.
    LeafId hit_test_pane_status_pill(int px, int py) const;

    // Access the active workspace's tree for divider/focus rendering.
    const SplitTree& active_tree() const;

    struct TabLayout
    {
        int col_begin; // first column of tab
        int col_end; // one past last column
        int text_col; // first column of label text
        int text_len; // label char count
        bool active;
        bool editing = false;
        std::string label;
    };

    struct LabelCluster
    {
        std::string text;
        int width = 1;
        Color fg{};
    };

    struct RightPillLayout
    {
        int col_begin = 0;
        int col_end = 0;
        int text_col = 0;
        Color bg{};
        Color accent_bg{}; // optional left accent color (drawn if accent_cols > 0)
        int accent_cols = 0; // number of columns covered by the left accent
        bool flat_right_edge = false;
        std::vector<LabelCluster> clusters;
    };

private:
    struct PaneStatusEntry
    {
        int pane_x = 0;
        int pane_y = 0;
        int pane_w = 0;
        int pane_h = 0;
        int index = 0; // 1-based pane number for the "N: " label prefix
        std::string text; // raw status text from host (no number prefix)
        bool focused = false;
        LeafId leaf = kInvalidLeaf;
    };

    void update_tab_grid(std::span<const TabLayout> tabs, std::span<const RightPillLayout> right_pills);
    void update_pane_status_grids(IFrameContext& frame, std::span<const PaneStatusEntry> entries);
    const ChromeTheme& theme() const;

    // Inline rename state. Either targets a workspace (Workspace) or a
    // pane (Pane); EditTarget::None means no rename in progress.
    enum class EditTarget
    {
        None,
        Workspace,
        Pane,
    };
    struct EditState
    {
        EditTarget target = EditTarget::None;
        int workspace_id = -1; // valid when target == Workspace
        LeafId leaf_id = kInvalidLeaf; // valid when target == Pane
        std::string buffer; // current edit text (no "N: " prefix)
        size_t cursor = 0; // UTF-8 byte offset within buffer
        std::chrono::steady_clock::time_point started_at{};
    };

    // Cached pane status pill rect from the last draw, used for hit testing.
    struct PaneStatusPillHit
    {
        LeafId leaf = kInvalidLeaf;
        float x = 0.0f;
        float y = 0.0f;
        float w = 0.0f;
        float h = 0.0f;
    };

    // Resolve a workspace id from a 1-based tab index, or -1 if out of range.
    int workspace_id_for_index(int tab_index) const;

    Deps deps_;
    std::unique_ptr<INanoVGPass> nanovg_pass_;
    std::unique_ptr<IGridHandle> tab_handle_;
    // One grid handle per visible pane status strip, keyed by leaf id. Reused
    // across frames; pruned when the leaf disappears.
    std::unordered_map<LeafId, std::unique_ptr<IGridHandle>> pane_status_handles_;
    HostViewport viewport_{};
    bool running_ = false;
    EditState edit_;
    // Refreshed during draw() so InputDispatcher's mouse routing can hit-test
    // the per-pane status pills without re-walking the active workspace tree.
    mutable std::vector<PaneStatusPillHit> pane_pill_hits_;
};

} // namespace draxul
