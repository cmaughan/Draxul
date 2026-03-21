#include "host_manager.h"

#include <draxul/app_config.h>
#include <draxul/base_renderer.h>
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

    if (launch.kind == HostKind::MegaCity)
    {
#ifdef DRAXUL_ENABLE_MEGACITY
        host_ = create_megacity_host();
#else
        error_ = "The Megacity host was disabled at build time (DRAXUL_ENABLE_MEGACITY=OFF).";
        return false;
#endif
    }
    else
        host_ = create_host(launch.kind);

    if (!host_)
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
        deps_.get_viewport ? deps_.get_viewport() : HostViewport{},
        display_ppi,
    };

    if (!host_->initialize(context, std::move(callbacks)))
    {
        error_ = host_->init_error();
        if (error_.empty())
            error_ = "Failed to initialize the selected host.";
        host_.reset();
        return false;
    }

    // Wire 3D renderer post-init for hosts that opt into I3DHost.
    // IGridRenderer IS-A I3DRenderer via inheritance — the upcast is always valid.
    if (auto* h3d = dynamic_cast<I3DHost*>(host_.get()))
    {
        if (deps_.grid_renderer)
            h3d->attach_3d_renderer(*static_cast<I3DRenderer*>(deps_.grid_renderer));
        if (deps_.imgui_host)
            h3d->attach_imgui_host(*deps_.imgui_host);
    }

    grid_renderer.set_default_background(host_->default_background());
    return true;
}

void HostManager::shutdown()
{
    if (host_)
    {
        host_->shutdown();
        host_.reset();
    }
}

} // namespace draxul
