#pragma once

#include <draxul/base_renderer.h>

#include <chrono>
#include <memory>

namespace draxul
{

class LoadedPlugin;
class PluginHost;

std::unique_ptr<IRenderPass> create_plugin_render_pass(
    std::shared_ptr<LoadedPlugin> plugin, void* instance,
    PluginHost& host,
    std::chrono::steady_clock::time_point started_at);

} // namespace draxul
