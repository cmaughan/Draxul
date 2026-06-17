#include <catch2/catch_all.hpp>

#include "fake_renderer.h"
#include "fake_window.h"
#include "temp_dir.h"

#include <draxul/kanban/kanban_host.h>
#include <draxul/text_service.h>

#include <SDL3/SDL.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

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

    explicit KanbanHostFixture(int card_count = 1, int column_count = 1, glm::ivec2 grid_size = { 80, 12 })
    {
        for (int column = 0; column < column_count; ++column)
        {
            const auto column_dir = temp.path / (column_count == 1 ? std::string("todo") : ("column-" + std::to_string(column + 1)));
            std::filesystem::create_directories(column_dir);
            for (int i = 0; i < card_count; ++i)
            {
                const auto path = column_dir / ("card-" + std::to_string(i + 1) + "-feature.md");
                std::ofstream(path) << "# Card " << (i + 1) << "\n";
                if (column == 0 && i == 0)
                    card_path = path;
            }
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
        viewport.grid_size = grid_size;

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

    REQUIRE(std::filesystem::weakly_canonical(fixture.callbacks.opened_markdown_path)
        == std::filesystem::weakly_canonical(fixture.card_path));
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

TEST_CASE("kanban host selection movement stays bounded on a large visible board", "[kanban][host][perf]")
{
    KanbanHostFixture fixture(200, 3, { 177, 35 });
    REQUIRE(fixture.renderer.last_handle != nullptr);
    fixture.renderer.last_handle->update_batches.clear();

    const auto start = std::chrono::steady_clock::now();
    fixture.host.on_key(key_event(SDLK_J));
    fixture.host.pump();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);

    REQUIRE(!fixture.renderer.last_handle->update_batches.empty());
    const auto& updates = fixture.renderer.last_handle->update_batches.back();
    INFO("selection movement should redraw touched card rows and status, not every visible card");
    REQUIRE(updates.size() < 1000);
    REQUIRE(elapsed < std::chrono::milliseconds(250));
}

TEST_CASE("kanban host repeats held selection keys without waiting for OS repeat", "[kanban][host][input]")
{
    KanbanHostFixture fixture(4);

    fixture.host.on_key(key_event(SDLK_J));
    REQUIRE(fixture.host.status_text().find("card-2-feature.md") != std::string::npos);

    std::this_thread::sleep_for(std::chrono::milliseconds(180));
    fixture.host.pump();

    REQUIRE(fixture.host.status_text().find("card-3-feature.md") != std::string::npos);
}

TEST_CASE("kanban host stops held-key repeat on key release", "[kanban][host][input]")
{
    KanbanHostFixture fixture(4);

    fixture.host.on_key(key_event(SDLK_J));
    REQUIRE(fixture.host.status_text().find("card-2-feature.md") != std::string::npos);

    fixture.host.on_key(KeyEvent{
        .scancode = 0,
        .keycode = SDLK_J,
        .mod = kModNone,
        .pressed = false,
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(180));
    fixture.host.pump();

    REQUIRE(fixture.host.status_text().find("card-2-feature.md") != std::string::npos);
}
