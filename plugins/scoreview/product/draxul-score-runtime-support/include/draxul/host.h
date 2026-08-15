#pragma once

#include <draxul/events.h>

#include <glm/glm.hpp>

#include <chrono>
#include <optional>
#include <string>

namespace draxul
{

using Color = glm::vec4;

struct HostLaunchOptions
{
    std::string command;
    std::string source_path;
    std::optional<Color> terminal_bg;
};

struct HostViewport
{
    glm::ivec2 pixel_pos{ 0 };
    glm::ivec2 pixel_size{ 0 };
    glm::ivec2 grid_size{ 1 };
    int padding = 0;
    float pixel_scale = 1.0f;
};

struct HostReloadConfig
{
    std::optional<Color> terminal_bg;
};

struct HostRuntimeState
{
    bool content_ready = false;
    std::chrono::steady_clock::time_point last_activity_time
        = std::chrono::steady_clock::now();
};

struct HostDebugState
{
    std::string name;
};

struct HostPrintHint
{
    glm::ivec2 content_pos{ 0 };
    glm::ivec2 content_size{ 0 };
    bool paper_white = false;
};

struct HostContext
{
    HostLaunchOptions launch_options;
    HostViewport initial_viewport;
    float display_ppi = 96.0f;
};

} // namespace draxul
