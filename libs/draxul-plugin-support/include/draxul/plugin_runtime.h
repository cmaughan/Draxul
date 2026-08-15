#pragma once

#include <chrono>
#include <cstddef>
#include <draxul/events.h>
#include <draxul/types.h>
#include <filesystem>
#include <glm/glm.hpp>
#include <string>
#include <string_view>

namespace draxul
{

class ConfigDocument;
class TextService;

struct PluginRuntimeLaunchOptions
{
    std::string source_path;
    bool request_continuous_refresh = false;
    bool show_ui_panels = true;
};

struct PluginRuntimeViewport
{
    glm::ivec2 pixel_pos{ 0 };
    glm::ivec2 pixel_size{ 0 };
    glm::ivec2 grid_size{ 1 };
    int padding = 0;
    float pixel_scale = 1.0f;
};

struct PluginRuntimeContext
{
    TextService* text_service = nullptr;
    ConfigDocument* config_document = nullptr;
    PluginRuntimeLaunchOptions launch_options;
    PluginRuntimeViewport initial_viewport;
    std::filesystem::path storage_directory;
    float display_ppi = 96.0f;
};

struct PluginRuntimeState
{
    bool content_ready = false;
    std::chrono::steady_clock::time_point last_activity_time =
        std::chrono::steady_clock::now();
};

struct PluginDebugState
{
    std::string name;
    int grid_cols = 0;
    int grid_rows = 0;
    size_t dirty_cells = 0;
};

class PluginRuntimeCallbacks
{
public:
    virtual ~PluginRuntimeCallbacks() = default;
    virtual void request_frame() = 0;
    virtual void request_quit() = 0;
    virtual bool dispatch_source_action(std::string_view /*action*/, bool /*keep_focus*/ = false)
    {
        return false;
    }
};

} // namespace draxul
