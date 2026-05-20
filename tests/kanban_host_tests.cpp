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

    explicit KanbanHostFixture(int card_count = 1)
    {
        const auto todo = temp.path / "todo";
        std::filesystem::create_directories(todo);
        for (int i = 0; i < card_count; ++i)
        {
            const auto path = todo / ("card-" + std::to_string(i + 1) + "-feature.md");
            std::ofstream(path) << "# Card " << (i + 1) << "\n";
            if (i == 0)
                card_path = path;
        }

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
    REQUIRE(fixture.host.status_text().find("card-1-feature.md") != std::string::npos);
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
    REQUIRE(fixture.callbacks.toast_message.find("card-1-feature.md") != std::string::npos);
}

TEST_CASE("kanban host selection movement updates a small dirty region", "[kanban][host][perf]")
{
    KanbanHostFixture fixture(4);
    REQUIRE(fixture.renderer.last_handle != nullptr);
    fixture.renderer.last_handle->update_batches.clear();

    fixture.host.on_key(key_event(SDLK_J));
    fixture.host.pump();

    REQUIRE(!fixture.renderer.last_handle->update_batches.empty());
    const auto& updates = fixture.renderer.last_handle->update_batches.back();
    INFO("single-row selection move should not redraw the full 80x12 pane");
    REQUIRE(updates.size() < 400);
}
