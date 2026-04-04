#pragma once

#include "split_tree.h"
#include <draxul/host.h>
#include <draxul/host_kind.h>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

namespace draxul
{

class IWindow;
class IGridRenderer;
class IImGuiHost;
class TextService;
struct AppOptions;
struct AppConfig;
class ConfigDocument;

// Owns the IHost instance(s), the SplitTree layout, and manages their lifecycle.
// App holds a HostManager and calls create() during initialisation.
class HostManager
{
public:
    static HostKind platform_default_split_host_kind();
    static HostKind split_host_kind_for(HostKind primary_kind);

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

        // Converts a PaneDescriptor (pixel region) to a full HostViewport (with cols/rows/padding).
        // Provided by App since it owns the font metrics and UI panel layout.
        std::function<HostViewport(const PaneDescriptor&)> compute_viewport;
    };

    explicit HostManager(Deps deps);

    // Creates and initialises the primary host, resetting the tree to a single leaf.
    // Returns false on failure; error() contains the reason.
    // If host_kind_override is set, it overrides the AppOptions host_kind.
    bool create(IHostCallbacks& callbacks, int pixel_w, int pixel_h,
        std::optional<HostKind> host_kind_override = std::nullopt);

    // Splits the focused leaf in the given direction and launches a new host.
    // Returns the new leaf's ID, or kInvalidLeaf on failure.
    LeafId split_focused(SplitDirection dir, IHostCallbacks& callbacks);

    // Splits the focused leaf with a specific host kind.
    LeafId split_focused(SplitDirection dir, HostKind kind, IHostCallbacks& callbacks);

    // Splits the focused leaf with explicit launch options (host kind, args, etc.).
    LeafId split_focused(SplitDirection dir, HostLaunchOptions launch, IHostCallbacks& callbacks);

    // Closes a leaf by ID — shuts down its host and collapses the tree.
    // Returns false if this is the last leaf.
    bool close_leaf(LeafId id);

    // Convenience: closes the currently focused leaf.
    bool close_focused();

    // Restarts the host in the focused pane. Shuts down the current host,
    // clears the pane, and relaunches with the same launch options.
    // Returns false on failure.
    bool restart_focused(IHostCallbacks& callbacks);

    // Swaps the focused pane with the next pane in spatial order.
    // Returns false if there are fewer than 2 panes.
    bool swap_focused_with_next();

    // Recomputes the tree layout and updates all host viewports.
    void recompute_viewports(int pixel_w, int pixel_h);
    void recompute_viewports(int origin_x, int origin_y, int pixel_w, int pixel_h);

    // Toggles pane zoom: expands the focused pane to fill the full window,
    // or restores the previous split layout if already zoomed.
    // pixel_w/pixel_h are the current window dimensions for viewport recomputation.
    void toggle_zoom(int pixel_w, int pixel_h);

    // Returns true if a pane is currently zoomed.
    bool is_zoomed() const
    {
        return zoomed_;
    }

    // Returns the zoomed leaf ID, or kInvalidLeaf if not zoomed.
    LeafId zoomed_leaf() const
    {
        return zoomed_leaf_;
    }

    // Shuts down and releases all hosts.
    void shutdown();

    // Returns the focused host, or null before create().
    IHost* host() const
    {
        return focused_host();
    }
    IHost* focused_host() const;
    LeafId focused_leaf() const
    {
        return tree_.focused();
    }
    void set_focused(LeafId id);

    // Move focus to the adjacent pane in the given direction. No-op if no neighbor exists.
    // Returns true if focus changed.
    bool focus_direction(FocusDirection direction);

    // Returns the host for a specific leaf.
    IHost* host_for(LeafId id) const;

    // Hit-test a point (physical pixels). Updates focus if a new leaf is hit.
    // Returns the host under the point, or null.
    IHost* host_at_point(int px, int py);

    // Visit all hosts (zero-cost: no std::function allocation at call site).
    template <typename F>
    void for_each_host(F&& fn) const
    {
        tree_.for_each_leaf([this, &fn](LeafId id, const PaneDescriptor&) {
            auto it = hosts_.find(id);
            if (it != hosts_.end() && it->second)
                fn(id, *it->second);
        });
    }

    int host_count() const
    {
        return tree_.leaf_count();
    }
    const SplitTree& tree() const
    {
        return tree_;
    }

    const std::string& error() const
    {
        return error_;
    }

private:
    bool create_host_for_leaf(LeafId id, IHostCallbacks& callbacks,
        HostLaunchOptions launch, bool is_primary);
    void update_all_viewports();
    void update_focus(LeafId new_id);

    Deps deps_;
    SplitTree tree_;
    std::unordered_map<LeafId, std::unique_ptr<IHost>> hosts_;
    std::unordered_map<LeafId, HostLaunchOptions> launch_options_;
    std::string error_;

    // Zoom state: when zoomed, the focused pane fills the full window.
    bool zoomed_ = false;
    LeafId zoomed_leaf_ = kInvalidLeaf;
    int zoom_pixel_w_ = 0;
    int zoom_pixel_h_ = 0;
};

} // namespace draxul
