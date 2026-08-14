#pragma once

#include <draxul/plugin_runtime.h>

namespace draxul::tests
{

class PluginRuntimeTestCallbacks final : public PluginRuntimeCallbacks
{
public:
    void request_frame() override { ++request_frame_calls; }
    void request_quit() override { ++request_quit_calls; }

    int request_frame_calls = 0;
    int request_quit_calls = 0;
};

} // namespace draxul::tests
