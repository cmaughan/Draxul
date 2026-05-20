#include <catch2/catch_all.hpp>

#include "fake_renderer.h"
#include "fake_window.h"
#include "temp_dir.h"

#include <draxul/kanban/kanban_host.h>
#include <draxul/text_service.h>

#include <SDL3/SDL.h>
#include <filesystem>
#include <fstream>

using namespace draxul;
using namespace draxul::kanban;

namespace
{

class KanbanCallbacks final : public IHostCallbacks
{
public:
    void request_frame() override
    {
        ++request_frame_calls;
    }

    void request_quit() override {}
    void wake_window() override {}

    void set_window_title(const std::string& title) override
    {
        window_title = title;
    }

    void set_text_input_area(int, int, int, int) override {}

    bool open_markdown_source(std::string_view path) override
    {
        opened_markdown_path = std::string(path);
        return open_markdown_result;
    }

    void push_toast(int level, std::string_view message) override
    {
        toast_level = level;
        toast_message = std::string(message);
    }

    int request_frame_calls = 0;
    bool open_markdown_result = true;
    std::string opened_markdown_path;
    std::string window_title;
    int toast_level = -1;
    std::string toast_message;
};

KeyEvent key_event(int keycode, ModifierFlags mod = kModNone)
{
    return KeyEvent{
        .scancode = 0,
        .keycode = keycode,
        .mod = mod,
        .pressed = true,
    };
}

struct KanbanHostFixture
{
    draxul::tests::TempDir temp{ "draxul-kanban-host" };
    draxul::tests::FakeWindow window;
    draxul::tests::FakeTermRenderer renderer;
    TextService text_service;
    KanbanCallbacks callbacks;
    KanbanHost host;
    std::filesystem::path card_path;

    KanbanHostFixture()
    {
        const auto todo = temp.path / "todo";
        std::filesystem::create_directories(todo);
        card_path = todo / "first-feature.md";
        std::ofstream(card_path) << "# First\n";

        TextServiceConfig text_config;
        text_config.font_path = (std::filesystem::path(DRAXUL_PROJECT_ROOT)
                                    / "fonts"
                                    / "JetBrainsMonoNerdFont-Regular.ttf")
                                    .string();
        REQUIRE(text_service.initialize(text_config, TextService::DEFAULT_POINT_SIZE, 96.0f));

        HostLaunchOptions launch;
        launch.kind = HostKind::Kanban;
        launch.source_path = temp.path.string();

        HostViewport viewport;
        viewport.grid_size = { 80, 12 };

        HostContext context{
            .window = &window,
            .grid_renderer = &renderer,
            .text_service = &text_service,
            .launch_options = launch,
            .initial_viewport = viewport,
        };
        REQUIRE(host.initialize(context, callbacks));
        host.pump();
    }
};

} // namespace

TEST_CASE("kanban host initializes and reports board status", "[kanban][host]")
{
    KanbanHostFixture fixture;

    REQUIRE(fixture.host.is_running());
    REQUIRE(fixture.callbacks.window_title == fixture.temp.path.filename().string());
    REQUIRE(fixture.host.status_text().find("kanban") != std::string::npos);
    REQUIRE(fixture.host.status_text().find("first-feature.md") != std::string::npos);
    REQUIRE(fixture.renderer.create_grid_handle_calls == 1);
}

TEST_CASE("kanban host opens selected card through markdown callback", "[kanban][host]")
{
    KanbanHostFixture fixture;

    fixture.host.on_key(key_event(SDLK_RETURN));

    REQUIRE(fixture.callbacks.opened_markdown_path == fixture.card_path.string());
}

TEST_CASE("kanban host reports toast when markdown open callback fails", "[kanban][host]")
{
    KanbanHostFixture fixture;
    fixture.callbacks.open_markdown_result = false;

    fixture.host.on_key(key_event(SDLK_RETURN));

    REQUIRE(fixture.callbacks.toast_level == 2);
    REQUIRE(fixture.callbacks.toast_message.find("first-feature.md") != std::string::npos);
}
