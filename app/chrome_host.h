#pragma once

#include "workspace.h"

#include <draxul/base_renderer.h>
#include <draxul/host.h>
#include <draxul/nanovg_pass.h>
#include <draxul/renderer.h>
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
    // Returns the workspace ID, or -1 on failure.
    int add_workspace(IHostCallbacks& callbacks, int pixel_w, int pixel_h);

    // Closes a workspace by ID. Returns false if it's the last workspace or ID not found.
    bool close_workspace(int workspace_id, IHostCallbacks& callbacks);

    // Activates a workspace by ID.
    void activate_workspace(int workspace_id);

    // Cycle to the next or previous workspace.
    void next_workspace();
    void prev_workspace();

    // Access the active workspace's HostManager.
    HostManager& active_host_manager();
    const HostManager& active_host_manager() const;

    // The active workspace's SplitTree (convenience).
    const SplitTree& active_tree() const;

    // Tab bar height in pixels. Returns 0 when there is only one workspace.
    int tab_bar_height() const;

    int workspace_count() const
    {
        return static_cast<int>(workspaces_.size());
    }
    int active_workspace_id() const;
    const std::string& last_create_error() const
    {
        return last_create_error_;
    }

private:
    HostManager::Deps make_host_manager_deps() const;
    void draw_dividers_and_focus(IFrameContext& frame);
    void draw_tab_bar(IFrameContext& frame);
    void update_tab_grid();
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
