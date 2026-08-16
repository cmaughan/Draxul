#pragma once

#include "fake_renderer.h"
#include "fake_window.h"
#include "test_host_callbacks.h"
#include "test_support.h"

#include <draxul/host.h>
#include <draxul/renderer.h>
#include <draxul/text_service.h>
#include <draxul/window.h>

#include <utility>

namespace draxul::tests
{

template <typename HostT>
struct TerminalHostFixture
{
    FakeWindow window;
    FakeTermRenderer renderer;
    TextService text_service;
    HostT host;
    TestHostCallbacks callbacks;
    bool ok = false;

    explicit TerminalHostFixture(int cols = 20, int rows = 5,
        HostLaunchOptions launch_options = {})
    {
        host.cols_ = cols;
        host.rows_ = rows;

        init_text_service(text_service);

        HostViewport vp;
        vp.grid_size = { cols, rows };

        HostContext ctx{
            .window = &window,
            .grid_renderer = &renderer,
            .text_service = &text_service,
            .launch_options = std::move(launch_options),
            .initial_viewport = vp,
        };
        ok = host.initialize(ctx, callbacks);
    }
};

} // namespace draxul::tests
