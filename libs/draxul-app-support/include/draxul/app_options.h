#pragma once

// Runtime application options that depend on renderer/window/font subsystem types.
// For pure config data types (AppConfig, AppConfigOverrides, etc.), include
// <draxul/app_config_types.h> instead.

#include <draxul/app_config_types.h>
#include <draxul/host_kind.h>
#include <draxul/renderer.h>
#include <draxul/window.h>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace draxul
{

class IHost;

struct AppOptions
{
    // Per-field overrides that take precedence over the loaded AppConfig.
    AppConfigOverrides config_overrides;

    // Runtime / process-lifetime flags -- not persisted to disk.
    bool load_user_config = true;
    bool save_user_config = true;
    bool activate_window_on_startup = true;
    bool show_diagnostics_on_startup = false;
    bool clamp_window_to_display = true;
    bool show_render_test_window = false;
    std::optional<float> override_display_ppi;
    int render_target_pixel_width = 0;
    int render_target_pixel_height = 0;
    HostKind host_kind = HostKind::Nvim;
    std::string host_command;
    std::vector<std::string> host_args;
    std::vector<std::string> startup_commands;
    std::string host_working_dir;

    // Optional factory overrides for testing -- leave null in production.
    // window_factory: returns a fully-initialized IWindow. Return nullptr to simulate failure.
    // renderer_create_fn: called instead of create_renderer(); return empty RendererBundle to fail.
    // host_factory: called instead of create_host(). Return nullptr to simulate failure.
    //   The returned IHost must not yet be initialized -- HostManager calls initialize() itself.
    std::function<std::unique_ptr<IWindow>()> window_factory;
    std::function<RendererBundle(int atlas_size)> renderer_create_fn;
    std::function<std::unique_ptr<IHost>(HostKind)> host_factory;
};

} // namespace draxul
