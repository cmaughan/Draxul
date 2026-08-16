#include <catch2/catch_all.hpp>

#include "support/fake_grid_host.h"
#include "support/fake_renderer.h"
#include "support/fake_window.h"
#include "support/test_host_callbacks.h"

#include <draxul/text_service.h>

#include <filesystem>
#include <string>

using namespace draxul;
using namespace draxul::tests;

namespace
{

struct GridHostCursorHarness
{
    FakeWindow window;
    FakeTermRenderer renderer;
    TextService text_service;
    FakeGridHost host;
    TestHostCallbacks callbacks;

    GridHostCursorHarness()
    {
        draxul::tests::init_text_service(text_service);

        HostViewport viewport;
        viewport.pixel_size = { 800, 600 };
        viewport.grid_size = { 20, 5 };

        HostContext context{
            .window = &window,
            .grid_renderer = &renderer,
            .text_service = &text_service,
            .initial_viewport = viewport,
            .display_ppi = 96.0f,
        };

        REQUIRE(host.initialize(context, callbacks));
        host.exercise_apply_grid_size(20, 5);
        REQUIRE(renderer.last_handle != nullptr);
    }
};

} // namespace

TEST_CASE("grid host: preserving blink on cursor move keeps hidden cursor hidden", "[grid_host][cursor]")
{
    GridHostCursorHarness h;
    auto* handle = h.renderer.last_handle;
    REQUIRE(handle != nullptr);

    CursorStyle style = {};
    style.shape = CursorShape::Block;
    style.bg = { 1.0f, 1.0f, 1.0f, 1.0f };
    style.fg = { 0.0f, 0.0f, 0.0f, 1.0f };

    h.host.exercise_set_cursor_position(1, 1);
    h.host.exercise_set_cursor_style(style, { 500, 400, 300 });

    const auto hide_deadline = h.host.next_deadline();
    REQUIRE(hide_deadline.has_value());

    handle->reset();
    REQUIRE(h.host.exercise_advance_cursor_blink(*hide_deadline));
    REQUIRE(handle->set_cursor_calls == 1);
    REQUIRE(handle->last_cursor.x == -1);
    REQUIRE(handle->last_cursor.y == -1);

    const auto restore_deadline = h.host.next_deadline();
    REQUIRE(restore_deadline.has_value());

    handle->reset();
    h.host.exercise_set_cursor_position_preserve_blink(5, 2);
    REQUIRE(h.host.next_deadline() == restore_deadline);
    REQUIRE(handle->set_cursor_calls == 0);

    handle->reset();
    REQUIRE(h.host.exercise_advance_cursor_blink(*restore_deadline));
    REQUIRE(handle->set_cursor_calls == 1);
    REQUIRE(handle->last_cursor.x == 5);
    REQUIRE(handle->last_cursor.y == 2);
}
