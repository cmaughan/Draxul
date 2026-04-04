#include "host_manager.h"

#include <draxul/app_config.h>
#include <draxul/base_renderer.h>
#include <draxul/grid_host_base.h>
#include <draxul/host_kind.h>
#include <draxul/log.h>
#include <draxul/perf_timing.h>
#ifdef DRAXUL_ENABLE_MEGACITY
#include <draxul/megacity_host.h>
#endif
#include <draxul/renderer.h>
#include <draxul/text_service.h>

namespace draxul
{

#ifdef DRAXUL_ENABLE_MEGACITY
std::unique_ptr<IHost> create_megacity_host();
#endif

namespace
{

bool is_terminal_shell_host(HostKind kind)
{
    using enum HostKind;
    switch (kind)
    {
    case PowerShell:
    case Bash:
    case Zsh:
    case Wsl:
        return true;
    case Nvim:
    case MegaCity:
        return false;
    }
    return false;
}

HostKind platform_default_split_host_kind_impl()
{
#ifdef _WIN32
    return HostKind::PowerShell;
#else
    return HostKind::Zsh;
#endif
}

} // namespace

HostManager::HostManager(Deps deps)
    : deps_(std::move(deps))
{
}

HostKind HostManager::platform_default_split_host_kind()
{
    return platform_default_split_host_kind_impl();
}

HostKind HostManager::split_host_kind_for(HostKind primary_kind)
{
    if (is_terminal_shell_host(primary_kind))
        return primary_kind;
    return platform_default_split_host_kind_impl();
}

bool HostManager::create(IHostCallbacks& callbacks, int pixel_w, int pixel_h,
    std::optional<HostKind> host_kind_override)
{
    PERF_MEASURE();
    error_.clear();
    hosts_.clear();
    launch_options_.clear();

    LeafId root_id = tree_.reset(pixel_w, pixel_h);

    HostLaunchOptions launch;
    launch.kind = host_kind_override.value_or(deps_.options->host_kind);
    launch.command = deps_.options->host_command;
    launch.args = deps_.options->host_args;
    launch.working_dir = deps_.options->host_working_dir;
    launch.startup_commands = deps_.options->startup_commands;
    launch.enable_ligatures = deps_.config->enable_ligatures;
    if (!deps_.config->terminal.fg.empty())
        launch.terminal_fg = parse_hex_color(deps_.config->terminal.fg);
    if (!deps_.config->terminal.bg.empty())
        launch.terminal_bg = parse_hex_color(deps_.config->terminal.bg);

    return create_host_for_leaf(root_id, callbacks, std::move(launch), true);
}

LeafId HostManager::split_focused(SplitDirection dir, IHostCallbacks& callbacks)
{
    PERF_MEASURE();
    LeafId focused = tree_.focused();
    if (focused == kInvalidLeaf)
        return kInvalidLeaf;

    LeafId new_id = tree_.split_leaf(focused, dir);
    if (new_id == kInvalidLeaf)
        return kInvalidLeaf;

    HostLaunchOptions launch;
    // Split panes open a shell by default. If the primary host is already a shell,
    // preserve that explicit shell choice; otherwise use the platform shell.
    const HostKind primary_kind = deps_.options ? deps_.options->host_kind : platform_default_split_host_kind_impl();
    launch.kind = split_host_kind_for(primary_kind);
    launch.enable_ligatures = deps_.config->enable_ligatures;
    if (!deps_.config->terminal.fg.empty())
        launch.terminal_fg = parse_hex_color(deps_.config->terminal.fg);
    if (!deps_.config->terminal.bg.empty())
        launch.terminal_bg = parse_hex_color(deps_.config->terminal.bg);
    if (deps_.options)
    {
        launch.working_dir = deps_.options->host_working_dir;
        if (is_terminal_shell_host(primary_kind) && launch.kind == primary_kind)
        {
            launch.command = deps_.options->host_command;
            launch.args = deps_.options->host_args;
            launch.startup_commands = deps_.options->startup_commands;
        }
    }

    if (!create_host_for_leaf(new_id, callbacks, std::move(launch), false))
    {
        // Rollback the tree split
        tree_.close_leaf(new_id);
        return kInvalidLeaf;
    }

    // Update all viewports (tree was recomputed by split_leaf)
    update_all_viewports();

    // Focus the new pane
    update_focus(new_id);

    return new_id;
}

LeafId HostManager::split_focused(SplitDirection dir, HostKind kind, IHostCallbacks& callbacks)
{
    HostLaunchOptions launch;
    launch.kind = kind;
    return split_focused(dir, std::move(launch), callbacks);
}

LeafId HostManager::split_focused(SplitDirection dir, HostLaunchOptions launch, IHostCallbacks& callbacks)
{
    PERF_MEASURE();
    LeafId focused = tree_.focused();
    if (focused == kInvalidLeaf)
        return kInvalidLeaf;

    LeafId new_id = tree_.split_leaf(focused, dir);
    if (new_id == kInvalidLeaf)
        return kInvalidLeaf;

    launch.enable_ligatures = deps_.config->enable_ligatures;
    if (!deps_.config->terminal.fg.empty())
        launch.terminal_fg = parse_hex_color(deps_.config->terminal.fg);
    if (!deps_.config->terminal.bg.empty())
        launch.terminal_bg = parse_hex_color(deps_.config->terminal.bg);
    if (deps_.options && launch.working_dir.empty())
        launch.working_dir = deps_.options->host_working_dir;

    if (!create_host_for_leaf(new_id, callbacks, std::move(launch), false))
    {
        tree_.close_leaf(new_id);
        return kInvalidLeaf;
    }

    update_all_viewports();
    update_focus(new_id);
    return new_id;
}

bool HostManager::close_leaf(LeafId id)
{
    PERF_MEASURE();
    if (tree_.leaf_count() <= 1)
        return false;

    auto it = hosts_.find(id);
    if (it == hosts_.end())
        return false;

    // Cancel zoom if the zoomed pane is being closed, or if closing reduces to one leaf.
    if (zoomed_ && (id == zoomed_leaf_ || tree_.leaf_count() <= 2))
    {
        zoomed_ = false;
        zoomed_leaf_ = kInvalidLeaf;
    }

    // Shut down the host
    if (it->second)
        it->second->shutdown();
    hosts_.erase(it);
    launch_options_.erase(id);

    // Collapse the tree (this also updates focus if needed)
    if (!tree_.close_leaf(id))
        return false;

    update_all_viewports();
    return true;
}

bool HostManager::close_focused()
{
    return close_leaf(tree_.focused());
}

bool HostManager::restart_focused(IHostCallbacks& callbacks)
{
    PERF_MEASURE();
    LeafId id = tree_.focused();
    if (id == kInvalidLeaf)
        return false;

    auto it = hosts_.find(id);
    if (it == hosts_.end())
        return false;

    // Retrieve saved launch options for this leaf.
    auto opts_it = launch_options_.find(id);
    if (opts_it == launch_options_.end())
    {
        error_ = "No launch options recorded for focused pane.";
        return false;
    }
    HostLaunchOptions launch = opts_it->second;

    // Shut down the current host.
    if (it->second)
        it->second->shutdown();
    hosts_.erase(it);

    // Relaunch the same host in the same pane slot.
    if (!create_host_for_leaf(id, callbacks, std::move(launch), false))
        return false;

    update_all_viewports();
    return true;
}

bool HostManager::swap_focused_with_next()
{
    PERF_MEASURE();
    LeafId focused = tree_.focused();
    if (focused == kInvalidLeaf)
        return false;

    LeafId next = tree_.next_leaf_after(focused);
    if (next == kInvalidLeaf)
        return false;

    // swap_leaves swaps the IDs stored in the tree nodes: the node at
    // position-A now holds ID B and vice versa.  update_all_viewports()
    // iterates the tree in spatial order, so hosts_[B] will receive
    // position-A's viewport — effectively swapping the two hosts'
    // on-screen positions.  The hosts_ and launch_options_ maps stay
    // unchanged because the keys still match the (now relocated) IDs.
    if (!tree_.swap_leaves(focused, next))
        return false;

    update_all_viewports();
    return true;
}

void HostManager::recompute_viewports(int pixel_w, int pixel_h)
{
    recompute_viewports(0, 0, pixel_w, pixel_h);
}

void HostManager::recompute_viewports(int origin_x, int origin_y, int pixel_w, int pixel_h)
{
    PERF_MEASURE();
    tree_.recompute(origin_x, origin_y, pixel_w, pixel_h);
    if (zoomed_)
    {
        zoom_pixel_w_ = pixel_w;
        zoom_pixel_h_ = pixel_h;
    }
    update_all_viewports();
}

void HostManager::toggle_zoom(int pixel_w, int pixel_h)
{
    PERF_MEASURE();
    if (tree_.leaf_count() <= 1)
        return; // Nothing to zoom with a single pane.

    if (zoomed_)
    {
        // Unzoom: restore normal viewports.
        zoomed_ = false;
        zoomed_leaf_ = kInvalidLeaf;
        update_all_viewports();
    }
    else
    {
        // Zoom: focused pane fills the full window.
        LeafId focused = tree_.focused();
        if (focused == kInvalidLeaf)
            return;

        zoomed_ = true;
        zoomed_leaf_ = focused;
        zoom_pixel_w_ = pixel_w;
        zoom_pixel_h_ = pixel_h;
        update_all_viewports();
    }
}

void HostManager::shutdown()
{
    PERF_MEASURE();
    for (auto& [id, host] : hosts_)
    {
        if (host)
        {
            host->shutdown();
            host.reset();
        }
    }
    hosts_.clear();
    launch_options_.clear();
}

IHost* HostManager::focused_host() const
{
    return host_for(tree_.focused());
}

void HostManager::set_focused(LeafId id)
{
    update_focus(id);
}

bool HostManager::focus_direction(FocusDirection direction)
{
    PERF_MEASURE();
    LeafId current = tree_.focused();
    if (current == kInvalidLeaf)
        return false;

    LeafId neighbor = tree_.find_neighbor(current, direction);
    if (neighbor == kInvalidLeaf)
        return false;

    update_focus(neighbor);
    return true;
}

void HostManager::update_focus(LeafId new_id)
{
    LeafId old_id = tree_.focused();
    if (old_id == new_id)
        return;

    if (IHost* old_host = host_for(old_id))
        old_host->on_focus_lost();

    tree_.set_focused(new_id);

    if (IHost* new_host = host_for(new_id))
        new_host->on_focus_gained();
}

IHost* HostManager::host_for(LeafId id) const
{
    auto it = hosts_.find(id);
    return it != hosts_.end() ? it->second.get() : nullptr;
}

IHost* HostManager::host_at_point(int px, int py)
{
    PERF_MEASURE();
    auto result = tree_.hit_test(px, py);
    if (const auto* leaf_hit = std::get_if<SplitTree::LeafHit>(&result))
    {
        update_focus(leaf_hit->id);
        return host_for(leaf_hit->id);
    }
    // If divider hit or miss, return focused host
    return focused_host();
}

bool HostManager::create_host_for_leaf(LeafId id, IHostCallbacks& callbacks,
    HostLaunchOptions launch, bool is_primary)
{
    PERF_MEASURE();
    std::unique_ptr<IHost> new_host;

    if (deps_.options && deps_.options->host_factory)
    {
        new_host = deps_.options->host_factory(launch.kind);
    }
    else if (launch.kind == HostKind::MegaCity)
    {
#ifdef DRAXUL_ENABLE_MEGACITY
        new_host = create_megacity_host();
#else
        error_ = "The Megacity host was disabled at build time (DRAXUL_ENABLE_MEGACITY=OFF).";
        return false;
#endif
    }
    else
        new_host = create_host(launch.kind);

    if (!new_host)
    {
        error_ = std::string("The selected host is not supported on this platform: ") + to_string(launch.kind);
        return false;
    }

#ifdef DRAXUL_ENABLE_MEGACITY
    if (launch.kind == HostKind::MegaCity)
    {
        if (auto* megacity = dynamic_cast<MegaCityHost*>(new_host.get()))
            megacity->set_continuous_refresh_enabled(deps_.options && deps_.options->megacity_continuous_refresh);
    }
#endif

    IGridRenderer& grid_renderer = *deps_.grid_renderer;
    const float display_ppi = deps_.display_ppi ? *deps_.display_ppi : 96.0f;

    PaneDescriptor desc = tree_.descriptor_for(id);
    HostViewport viewport = deps_.compute_viewport ? deps_.compute_viewport(desc) : HostViewport{};

    // Save launch options before moving them into the context.
    HostLaunchOptions saved_launch = launch;

    if (HostContext context{
            .window = deps_.window,
            .grid_renderer = &grid_renderer,
            .text_service = deps_.text_service,
            .config = deps_.config,
            .config_document = deps_.config_document,
            .launch_options = std::move(launch),
            .initial_viewport = viewport,
            .owner_lifetime = deps_.owner_lifetime,
            .display_ppi = display_ppi,
        };
        !new_host->initialize(context, callbacks))
    {
        error_ = new_host->init_error();
        if (error_.empty())
            error_ = "Failed to initialize the selected host.";
        return false;
    }

#ifdef DRAXUL_ENABLE_MEGACITY
    if (deps_.options && deps_.options->no_ui)
    {
        if (auto* megacity = dynamic_cast<MegaCityHost*>(new_host.get()))
            megacity->set_ui_panels_visible(false);
    }
#endif

    if (deps_.imgui_host)
        new_host->attach_imgui_host(*deps_.imgui_host);

    if (is_primary)
        grid_renderer.set_default_background(new_host->default_background());

    hosts_[id] = std::move(new_host);
    launch_options_[id] = std::move(saved_launch);
    return true;
}

void HostManager::update_all_viewports()
{
    PERF_MEASURE();
    if (!deps_.compute_viewport)
        return;

    if (zoomed_)
    {
        // When zoomed, only the focused pane gets a viewport update.
        // Hidden panes are left untouched — calling set_viewport({0,0})
        // would trigger a grid resize to 1x1 in the child process
        // (nvim, shell) which is both wasteful and disruptive.
        PaneDescriptor full_desc{ { 0, 0 }, { zoom_pixel_w_, zoom_pixel_h_ } };

        auto it = hosts_.find(zoomed_leaf_);
        if (it != hosts_.end() && it->second)
            it->second->set_viewport(deps_.compute_viewport(full_desc));
    }
    else
    {
        tree_.for_each_leaf([this](LeafId id, const PaneDescriptor& desc) {
            auto it = hosts_.find(id);
            if (it != hosts_.end() && it->second)
                it->second->set_viewport(deps_.compute_viewport(desc));
        });
    }
}

} // namespace draxul
