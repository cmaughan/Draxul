#pragma once

#include "split_tree.h"
#include <draxul/host.h>
#include <draxul/host_kind.h>
#include <functional>
#include <memory>
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
    bool create(IHostCallbacks& callbacks, int pixel_w, int pixel_h);

    // Splits the focused leaf in the given direction and launches a new host.
    // Returns the new leaf's ID, or kInvalidLeaf on failure.
    LeafId split_focused(SplitDirection dir, IHostCallbacks& callbacks);

    // Closes a leaf by ID — shuts down its host and collapses the tree.
    // Returns false if this is the last leaf.
    bool close_leaf(LeafId id);

    // Convenience: closes the currently focused leaf.
    bool close_focused();

    // Recomputes the tree layout and updates all host viewports.
    void recompute_viewports(int pixel_w, int pixel_h);

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

    Deps deps_;
    SplitTree tree_;
    std::unordered_map<LeafId, std::unique_ptr<IHost>> hosts_;
    std::string error_;
};

} // namespace draxul
