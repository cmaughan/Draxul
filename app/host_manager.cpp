#include "host_manager.h"

#include <draxul/app_config.h>
#include <draxul/base_renderer.h>
#include <draxul/grid_host_base.h>
#include <draxul/host_kind.h>
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

bool HostManager::create(IHostCallbacks& callbacks, int pixel_w, int pixel_h)
{
    error_.clear();
    hosts_.clear();

    LeafId root_id = tree_.reset(pixel_w, pixel_h);

    HostLaunchOptions launch;
    launch.kind = deps_.options->host_kind;
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
    tree_.set_focused(new_id);

    return new_id;
}

bool HostManager::close_leaf(LeafId id)
{
    if (tree_.leaf_count() <= 1)
        return false;

    auto it = hosts_.find(id);
    if (it == hosts_.end())
        return false;

    // Shut down the host
    if (it->second)
        it->second->shutdown();
    hosts_.erase(it);

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

void HostManager::recompute_viewports(int pixel_w, int pixel_h)
{
    tree_.recompute(pixel_w, pixel_h);
    update_all_viewports();
}

void HostManager::shutdown()
{
    for (auto& [id, host] : hosts_)
    {
        if (host)
        {
            host->shutdown();
            host.reset();
        }
    }
    hosts_.clear();
}

IHost* HostManager::focused_host() const
{
    return host_for(tree_.focused());
}

void HostManager::set_focused(LeafId id)
{
    tree_.set_focused(id);
}

IHost* HostManager::host_for(LeafId id) const
{
    auto it = hosts_.find(id);
    return it != hosts_.end() ? it->second.get() : nullptr;
}

IHost* HostManager::host_at_point(int px, int py)
{
    auto result = tree_.hit_test(px, py);
    if (const auto* leaf_hit = std::get_if<SplitTree::LeafHit>(&result))
    {
        tree_.set_focused(leaf_hit->id);
        return host_for(leaf_hit->id);
    }
    // If divider hit or miss, return focused host
    return focused_host();
}

bool HostManager::create_host_for_leaf(LeafId id, IHostCallbacks& callbacks,
    HostLaunchOptions launch, bool is_primary)
{
    std::unique_ptr<IHost> new_host;

    if (deps_.options && deps_.options->host_factory)
    {
        new_host = deps_.options->host_factory(launch.kind);
    }
    else if (is_primary && launch.kind == HostKind::MegaCity)
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

    if (HostContext context{
            deps_.window,
            &grid_renderer,
            deps_.text_service,
            std::move(launch),
            deps_.config,
            deps_.config_document,
            viewport,
            deps_.owner_lifetime,
            display_ppi,
        };
        !new_host->initialize(context, callbacks))
    {
        error_ = new_host->init_error();
        if (error_.empty())
            error_ = "Failed to initialize the selected host.";
        return false;
    }

    // Wire 3D renderer post-init for hosts that opt into I3DHost.
    if (auto* h3d = dynamic_cast<I3DHost*>(new_host.get()))
    {
        if (deps_.grid_renderer)
            h3d->attach_3d_renderer(*static_cast<I3DRenderer*>(deps_.grid_renderer));
    }

    if (is_primary)
        grid_renderer.set_default_background(new_host->default_background());

    hosts_[id] = std::move(new_host);
    return true;
}

void HostManager::update_all_viewports()
{
    if (!deps_.compute_viewport)
        return;

    tree_.for_each_leaf([this](LeafId id, const PaneDescriptor& desc) {
        auto it = hosts_.find(id);
        if (it != hosts_.end() && it->second)
            it->second->set_viewport(deps_.compute_viewport(desc));
    });
}

} // namespace draxul
