#pragma once

#include "workspace.h"

#include <draxul/base_renderer.h>
#include <draxul/host.h>
#include <draxul/host_kind.h>
#include <draxul/nanovg_pass.h>
#include <draxul/renderer.h>
#include <optional>
#include <span>
#include <vector>

struct NVGcontext;

namespace draxul
{

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
    std::optional<std::chrono::steady_clock::time_point> next_deadline() const override
    {
        return std::nullopt;
    }

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

    // --- Workspace / Tab API ---

    // Creates the first workspace with a primary host. Called once during init.
    bool create_initial_workspace(IHostCallbacks& callbacks, int pixel_w, int pixel_h);

    // Creates a new workspace, initializes its primary host, and activates it.
    // If host_kind is set, that host is launched; otherwise the platform default shell.
    // Returns the workspace ID, or -1 on failure.
    int add_workspace(IHostCallbacks& callbacks, int pixel_w, int pixel_h,
        std::optional<HostKind> host_kind = std::nullopt);

    // Closes a workspace by ID. Returns false if it's the last workspace or ID not found.
    bool close_workspace(int workspace_id, IHostCallbacks& callbacks);

    // Activates a workspace by ID.
    void activate_workspace(int workspace_id);

    // Cycle to the next or previous workspace.
    void next_workspace();
    void prev_workspace();

    // Activate workspace by 1-based display index. No-op if out of range.
    void activate_workspace_by_index(int one_based_index);

    // Access the active workspace's HostManager.
    HostManager& active_host_manager();
    const HostManager& active_host_manager() const;

    // The active workspace's SplitTree (convenience).
    const SplitTree& active_tree() const;

    // Tab bar height in pixels. Returns 0 when there is only one workspace.
    int tab_bar_height() const;

    // Hit-test a point (physical pixels) against the tab bar.
    // Returns the 1-based tab index if hit, or 0 if not in the tab bar.
    int hit_test_tab(int px, int py) const;

    // Recompute viewports for ALL workspaces (not just active).
    void recompute_all_viewports(int origin_x, int origin_y, int pixel_w, int pixel_h);

    int workspace_count() const
    {
        return static_cast<int>(workspaces_.size());
    }
    int active_workspace_id() const;
    const std::string& last_create_error() const
    {
        return last_create_error_;
    }

    struct TabLayout
    {
        int col_begin; // first column of tab
        int col_end; // one past last column
        int text_col; // first column of label text
        int text_len; // label char count
        bool active;
        std::string label;
    };

private:
    HostManager::Deps make_host_manager_deps() const;
    void update_tab_grid(std::span<const TabLayout> tabs);
    void flush_atlas_if_dirty();

    Deps deps_;
    std::vector<std::unique_ptr<Workspace>> workspaces_;
    int active_workspace_ = -1;
    int next_workspace_id_ = 0;

    std::unique_ptr<INanoVGPass> nanovg_pass_;
    std::unique_ptr<IGridHandle> tab_handle_;
    HostViewport viewport_{};
    bool running_ = false;
    std::string last_create_error_;
};

} // namespace draxul
