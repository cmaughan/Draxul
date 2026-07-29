#pragma once

#include "split_tree.h"
#include <chrono>
#include <draxul/agent_model.h>
#include <draxul/host.h>
#include <draxul/host_kind.h>
#include <draxul/session_model.h>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

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
// App holds a PaneManager and calls create() during initialisation.
class PaneManager
{
public:
    static HostKind platform_default_split_host_kind();
    static HostKind split_host_kind_for(HostKind primary_kind);

    using SavedLaunchOptions = SessionSavedLaunchOptions;
    using PaneSnapshot = SessionPaneSnapshot;
    using PaneLayoutSnapshot = SessionPaneLayoutSnapshot;

    struct Deps
    {
        const AppOptions* options = nullptr;
        // Space-scoped working directory used when a launch does not provide
        // one explicitly. Empty falls back to AppOptions::host_working_dir.
        std::string default_working_dir;
        AppConfig* config = nullptr;
        ConfigDocument* config_document = nullptr;
        IWindow* window = nullptr;
        IGridRenderer* grid_renderer = nullptr;
        IImGuiHost* imgui_host = nullptr;
        TextService* text_service = nullptr;
        const float* display_ppi = nullptr;
        std::weak_ptr<void> owner_lifetime;
        // False when this manager is a projection of server-authoritative
        // topology. Projection reconciliation can still change the tree,
        // while local split commands and divider drags cannot diverge it.
        bool allow_local_layout_mutation = true;
        // A projected manager remains interactive, but reports the desired
        // ratio to App so the server can publish the authoritative tree.
        std::function<void(DividerId, float)>
            request_projected_divider_ratio;
        // Called while a host is still alive, immediately before PaneManager
        // destroys it. This lets non-owning routes such as InputDispatcher
        // release their pointer safely.
        std::function<void(IHost*)> before_host_destroyed;

        // Converts a PaneDescriptor (pixel region) to a full HostViewport (with cols/rows/padding).
        // Provided by App since it owns the font metrics and UI panel layout.
        std::function<HostViewport(const PaneDescriptor&)> compute_viewport;
    };

    explicit PaneManager(Deps deps);

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
    bool restart_leaf(LeafId id, IHostCallbacks& callbacks);

    // Swaps the focused pane with the next pane in spatial order.
    // Returns false if there are fewer than 2 panes.
    bool swap_focused_with_next();

    // Reset all split ratios to 50/50 and recompute viewports.
    void equalize_splits(IHostCallbacks& callbacks);

    // Recomputes the tree layout and updates all host viewports.
    void recompute_viewports(int pixel_w, int pixel_h);
    void recompute_viewports(int origin_x, int origin_y, int pixel_w, int pixel_h);

    // Companion Markdown preview pane (Kanban card preview). The preview is a
    // leaf split horizontally below `owner`, sized so `owner` keeps `top_ratio`
    // of the height and the preview takes the rest. The first call creates the
    // pane and loads `path`; later calls reuse the pane and just reload `path`.
    // Focus is left unchanged. Returns the preview leaf id, or kInvalidLeaf on
    // failure.
    LeafId show_markdown_preview(
        LeafId owner, float top_ratio, std::string_view path, IHostCallbacks& callbacks);

    // Closes the companion Markdown preview pane, if any, and collapses the
    // split so the owner reclaims the space.
    void hide_markdown_preview();

    // Whether a companion Markdown preview pane is currently open.
    bool has_markdown_preview() const
    {
        return markdown_preview_leaf_ != kInvalidLeaf;
    }

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

    // User-set pane name override (WI 128 — pane rename). When non-empty,
    // ChromeHost displays this in place of the host's status_text() for the
    // pane status pill. Cleared by passing an empty string. Persisted only
    // for the lifetime of the leaf — closing the pane drops the override.
    void set_pane_name(LeafId id, std::string name);
    const std::string& pane_name(LeafId id) const;
    bool has_pane_name(LeafId id) const;
    const std::string& pane_id(LeafId id) const;
    void set_agent_identity(LeafId id, AgentIdentity identity,
        AgentRestorePolicy restore_policy = AgentRestorePolicy::ResumeIfAvailable);
    const AgentIdentity* agent_identity(LeafId id) const;
    bool clear_agent_identity(LeafId id);
    bool set_agent_session_ref(LeafId id, AgentSessionRef session_ref);
    const AgentSessionRef* agent_session_ref(LeafId id) const;
    AgentRuntimeGeneration agent_runtime_generation(LeafId id) const;
    std::chrono::steady_clock::time_point agent_runtime_started_at(LeafId id) const;

    // Hit-test a point (physical pixels). Updates focus if a new leaf is hit.
    // Returns the host under the point, or null.
    IHost* host_at_point(int px, int py);

    // Hit-test for a divider at the given pixel. Returns the divider id and
    // direction if a divider is under the point, otherwise nullopt. Does not
    // change focus. Used by InputDispatcher to drive cursor feedback and drag.
    struct DividerHitInfo
    {
        DividerId id = kInvalidDivider;
        SplitDirection direction = SplitDirection::Vertical;
    };
    std::optional<DividerHitInfo> divider_at_point(int px, int py) const;

    // Update a divider's ratio based on a mouse pixel position and re-layout
    // viewports. Used during drag. The SplitTree retains its own origin/size
    // from the most recent recompute_viewports(), so the chrome reservation
    // is preserved without callers needing to re-supply it. cell_w/cell_h
    // quantize the divider position to cell boundaries (tmux-style); PaneManager
    // picks the relevant axis from the divider's split direction. Pass 0/0
    // to disable snapping.
    void update_divider_from_pixel(DividerId id, int px, int py, int cell_w = 0, int cell_h = 0);

    // Nudge a divider by a fixed delta (positive grows the first child).
    // cell_w/cell_h quantize the resulting divider position to cell boundaries.
    void nudge_divider(DividerId id, float delta, int cell_w = 0, int cell_h = 0);
    std::optional<float> divider_ratio(DividerId id) const;

    // Find an ancestor divider above the focused leaf in the given direction.
    DividerId find_focused_ancestor_divider(FocusDirection direction) const;

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
    bool has_restorable_shell_session() const;
    bool should_preserve_dead_leaf(LeafId id) const;
    bool is_server_owned_remote_terminal_leaf(LeafId id) const;
    std::optional<PaneLayoutSnapshot> snapshot_layout() const;
    bool restore_layout(
        IHostCallbacks& callbacks, int pixel_w, int pixel_h, const PaneLayoutSnapshot& state);
    // Apply an authoritative projected tree while preserving hosts whose
    // stable leaf identity and launch kind are unchanged. Unlike
    // restore_layout(), this is suitable for live server topology updates:
    // splitting or resizing one pane must not restart every surviving
    // client-local host.
    bool reconcile_projected_layout(
        IHostCallbacks& callbacks, int pixel_w, int pixel_h,
        const PaneLayoutSnapshot& state);
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
    std::unordered_map<LeafId, std::string> pane_user_names_;
    std::unordered_map<LeafId, std::string> pane_ids_;
    std::unordered_map<LeafId, AgentIdentity> agent_identities_;
    std::unordered_map<LeafId, AgentRestorePolicy> agent_restore_policies_;
    std::unordered_map<LeafId, AgentSessionRef> agent_session_refs_;
    std::unordered_map<LeafId, AgentRuntimeGeneration> runtime_generations_;
    std::unordered_map<LeafId, std::chrono::steady_clock::time_point>
        runtime_started_at_;
    uint64_t next_runtime_generation_ = 1;
    std::string error_;
    uint64_t next_pane_serial_ = 1;

    // Zoom state: when zoomed, the focused pane fills the full window.
    bool zoomed_ = false;
    LeafId zoomed_leaf_ = kInvalidLeaf;
    int zoom_pixel_w_ = 0;
    int zoom_pixel_h_ = 0;

    // Companion Markdown preview pane (Kanban card preview). kInvalidLeaf when
    // no preview is open. owner tracks the pane it is attached to so both refs
    // can be cleared if either leaf closes.
    LeafId markdown_preview_leaf_ = kInvalidLeaf;
    LeafId markdown_preview_owner_ = kInvalidLeaf;
};

} // namespace draxul
