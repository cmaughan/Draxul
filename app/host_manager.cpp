#include "host_manager.h"

#include <draxul/app_config.h>
#include <draxul/base_renderer.h>
#include <draxul/grid_host_base.h>
#include <draxul/host_kind.h>
#include <draxul/renderer.h>
#include <draxul/text_service.h>

namespace draxul
{

#ifdef DRAXUL_ENABLE_MEGACITY
std::unique_ptr<IHost> create_megacity_host();
#endif

HostManager::HostManager(Deps deps)
    : deps_(std::move(deps))
{
}

bool HostManager::create(HostCallbacks callbacks)
{
    error_.clear();

    HostLaunchOptions launch;
    launch.kind = deps_.options->host_kind;
    launch.command = deps_.options->host_command;
    launch.args = deps_.options->host_args;
    launch.working_dir = deps_.options->host_working_dir;
    launch.startup_commands = deps_.options->startup_commands;
    launch.enable_ligatures = deps_.config->enable_ligatures;

    HostViewport viewport = deps_.get_viewport ? deps_.get_viewport() : HostViewport{};
    return create_slot(std::move(callbacks), viewport, std::move(launch), true);
}

bool HostManager::add_slot(HostCallbacks callbacks, HostViewport viewport)
{
    HostLaunchOptions launch;
    // New panes always open a Zsh shell. A future host-picker UI will allow the user to
    // choose the host kind at split time.
    launch.kind = HostKind::Zsh;
    launch.enable_ligatures = deps_.config->enable_ligatures;

    return create_slot(std::move(callbacks), viewport, std::move(launch), false);
}

bool HostManager::create_slot(HostCallbacks callbacks, HostViewport viewport,
    HostLaunchOptions launch, bool is_primary)
{
    std::unique_ptr<IHost> new_host;

    if (is_primary && launch.kind == HostKind::MegaCity)
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

    IGridRenderer& grid_renderer = *deps_.grid_renderer;
    const float display_ppi = deps_.display_ppi ? *deps_.display_ppi : 96.0f;

    HostContext context{
        *deps_.window,
        grid_renderer,
        *deps_.text_service,
        launch,
        viewport,
        display_ppi,
    };

    if (!new_host->initialize(context, std::move(callbacks)))
    {
        error_ = new_host->init_error();
        if (error_.empty())
            error_ = "Failed to initialize the selected host.";
        return false;
    }

    // Wire 3D renderer post-init for hosts that opt into I3DHost.
    // IGridRenderer IS-A I3DRenderer via inheritance — the upcast is always valid.
    if (auto* h3d = dynamic_cast<I3DHost*>(new_host.get()))
    {
        if (deps_.grid_renderer)
            h3d->attach_3d_renderer(*static_cast<I3DRenderer*>(deps_.grid_renderer));
        if (deps_.imgui_host)
            h3d->attach_imgui_host(*deps_.imgui_host);
    }

    if (is_primary)
        grid_renderer.set_default_background(new_host->default_background());

    slots_.push_back({ std::move(new_host) });
    return true;
}

void HostManager::shutdown()
{
    for (auto& slot : slots_)
    {
        if (slot.host)
        {
            slot.host->shutdown();
            slot.host.reset();
        }
    }
    slots_.clear();
}

void HostManager::remove_slot(int index)
{
    if (index < 0 || index >= slot_count())
        return;
    if (slots_[static_cast<size_t>(index)].host)
        slots_[static_cast<size_t>(index)].host->shutdown();
    slots_.erase(slots_.begin() + index);
    // Keep focused_slot_ valid: if we removed a slot before (or at) the focused one, decrement.
    if (focused_slot_ > index || focused_slot_ >= slot_count())
        focused_slot_ = std::max(0, slot_count() - 1);
}

} // namespace draxul
