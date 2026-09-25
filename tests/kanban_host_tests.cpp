#include <catch2/catch_all.hpp>

#include "fake_renderer.h"
#include "fake_window.h"
#include "support/kanban_directory_scan_test_support.h"
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

    bool dispatch_to_nvim_host(std::string_view action, bool keep_focus) override
    {
        nvim_action = std::string(action);
        nvim_keep_focus = keep_focus;
        ++dispatch_nvim_calls;
        return dispatch_nvim_result;
    }

    bool show_markdown_preview(std::string_view path) override
    {
        preview_path = std::string(path);
        ++show_preview_calls;
        preview_visible = true;
        return show_preview_result;
    }

    void hide_markdown_preview() override
    {
        ++hide_preview_calls;
        preview_visible = false;
    }

    bool is_markdown_preview_visible() const override
    {
        return preview_visible;
    }

    void push_toast(int level, std::string_view message) override
    {
        toast_level = level;
        toast_message = std::string(message);
    }

    int request_frame_calls = 0;
    bool dispatch_nvim_result = true;
    int dispatch_nvim_calls = 0;
    std::string nvim_action;
    bool nvim_keep_focus = false;
    std::string window_title;
    int toast_level = -1;
    std::string toast_message;

    bool show_preview_result = true;
    int show_preview_calls = 0;
    int hide_preview_calls = 0;
    bool preview_visible = false;
    std::string preview_path;
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
    std::filesystem::path board_root;
    std::filesystem::path card_path;

    explicit KanbanHostFixture(
        int card_count = 1,
        int column_count = 1,
        glm::ivec2 grid_size = { 80, 12 },
        bool populate_all_columns = true,
        bool zero_pad_cards = false,
        bool aggregate_sources = false)
    {
        board_root = aggregate_sources ? temp.path / "workspace" / "kanban" : temp.path;
        for (int column = 0; column < column_count; ++column)
        {
            const auto column_dir = board_root / (column_count == 1 ? std::string("todo") : ("column-" + std::to_string(column + 1)));
            std::filesystem::create_directories(column_dir);
            const int cards_in_column = (populate_all_columns || column == 0) ? card_count : 0;
            for (int i = 0; i < cards_in_column; ++i)
            {
                const std::string number = zero_pad_cards
                    ? std::string(i + 1 < 10 ? "00" : (i + 1 < 100 ? "0" : "")) + std::to_string(i + 1)
                    : std::to_string(i + 1);
                const auto path = column_dir / ("card-" + number + "-feature.md");
                std::ofstream(path) << "# Card " << (i + 1) << "\n";
                if (column == 0 && i == 0)
                    card_path = path;
            }
        }

        if (aggregate_sources)
        {
            const auto workspace = board_root.parent_path();
            const auto product_todo = workspace / "plugins" / "product" / "kanban" / "todo";
            std::filesystem::create_directories(product_todo);
            std::ofstream(product_todo / "product-feature.md") << "# Product card\n";
            std::ofstream(workspace / ".gitmodules")
                << "[submodule \"plugins/product\"]\n"
                   "  path = plugins/product\n";
        }

        draxul::tests::init_text_service(text_service);

        HostLaunchOptions launch;
        launch.kind = HostKind::Kanban;
        launch.source_path = board_root.string();

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

TEST_CASE("kanban follows external lane moves and refreshes submodule cards automatically",
    "[kanban][host][file-monitor]")
{
    KanbanHostFixture fixture(1, 1, { 80, 12 }, true, false, true);
    fixture.host.on_focus_gained();
    const auto pump_until = [&](auto predicate) {
        const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        do
        {
            fixture.host.pump();
            if (predicate())
                return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        } while (std::chrono::steady_clock::now() < limit);
        return false;
    };
    // Wait out the single post-arming reconciliation before exercising events.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    fixture.host.pump();
    fixture.host.on_key(key_event(SDLK_P));
    const auto destination = std::filesystem::weakly_canonical(fixture.board_root)
        / "done" / fixture.card_path.filename();
    std::filesystem::create_directory(destination.parent_path());
    std::filesystem::rename(fixture.card_path, destination);
    REQUIRE(pump_until([&] { return fixture.callbacks.preview_path == destination.string(); }));
    CHECK(fixture.host.status_text().find("card-1-feature.md") != std::string::npos);

    const auto product = fixture.board_root.parent_path() / "plugins" / "product" / "kanban" / "todo";
    std::ofstream(product / "added-feature.md") << "# Added externally\n";
    REQUIRE(pump_until([&] { return fixture.host.status_text().find("3 cards") != std::string::npos; }));
    std::filesystem::remove(product / "added-feature.md");
    REQUIRE(pump_until([&] { return fixture.host.status_text().find("2 cards") != std::string::npos; }));

    const int previews = fixture.callbacks.show_preview_calls;
    std::ofstream(destination) << "# Edited externally\n";
    REQUIRE(pump_until([&] { return fixture.callbacks.show_preview_calls > previews; }));
    CHECK(fixture.callbacks.preview_path == destination.string());

    fixture.host.on_focus_lost();
    const int inactive_previews = fixture.callbacks.show_preview_calls;
    std::ofstream(product / "background-feature.md") << "# Background edit\n";
    REQUIRE(pump_until([&] { return fixture.host.status_text().find("3 cards") != std::string::npos; }));
    CHECK(fixture.callbacks.show_preview_calls == inactive_previews);
    fixture.host.on_focus_gained();
    CHECK(fixture.callbacks.show_preview_calls > inactive_previews);
    fixture.host.shutdown();
    CHECK_FALSE(fixture.host.is_running());
    CHECK_FALSE(fixture.host.next_deadline().has_value());
}

TEST_CASE("kanban host aggregates and filters repository boards with b",
    "[kanban][host][workspace][input]")
{
    KanbanHostFixture fixture(1, 1, { 80, 12 }, true, false, true);

    CHECK(fixture.host.status_text().find("2 cards") != std::string::npos);
    CHECK(fixture.host.status_text().find("2 boards") != std::string::npos);
    CHECK(fixture.host.status_text().find("filter: all") != std::string::npos);
    CHECK(fixture.host.status_text().find("[workspace] card-1-feature.md")
        != std::string::npos);

    fixture.host.on_key(key_event(SDLK_B));
    fixture.host.pump();
    CHECK(fixture.host.status_text().find("filter: workspace") != std::string::npos);
    CHECK(fixture.host.status_text().find("[workspace] card-1-feature.md")
        != std::string::npos);

    fixture.host.on_key(key_event(SDLK_B));
    fixture.host.pump();
    CHECK(fixture.host.status_text().find("filter: product") != std::string::npos);
    CHECK(fixture.host.status_text().find("[product] product-feature.md")
        != std::string::npos);

    fixture.host.on_key(key_event(SDLK_B));
    fixture.host.pump();
    CHECK(fixture.host.status_text().find("filter: all") != std::string::npos);
}

TEST_CASE("kanban host preserves its board when a reload scan fails",
    "[kanban][host][reload][scan-error]")
{
    KanbanHostFixture fixture;
    const auto column = fixture.temp.path / "todo";
    const auto replacement
        = column / "replacement-feature.md";
    std::filesystem::rename(fixture.card_path, replacement);

    const auto verify_reload_failure
        = [&](const std::filesystem::path& directory,
              draxul::tests::KanbanScanFailurePoint point) {
              draxul::tests::FaultInjectingKanbanDirectoryOperations
                  operations(directory, point);
              bool reloaded = true;
              {
                  ScopedKanbanDirectoryOperationsOverride override(
                      operations);
                  CHECK_NOTHROW(
                      reloaded = fixture.host.dispatch_action("reload"));
              }
              CHECK_FALSE(reloaded);
              CHECK(operations.injected_failures == 1);
              CHECK_FALSE(fixture.host.init_error().empty());
              CHECK(fixture.host.status_text().find(
                        "card-1-feature.md")
                  != std::string::npos);
              CHECK(fixture.host.status_text().find(
                        "replacement-feature.md")
                  == std::string::npos);
          };

    SECTION("root construction failure")
    {
        verify_reload_failure(fixture.temp.path,
            draxul::tests::KanbanScanFailurePoint::Construction);
    }
    SECTION("column advancement failure")
    {
        verify_reload_failure(column,
            draxul::tests::KanbanScanFailurePoint::Advancement);
    }

    REQUIRE(fixture.host.dispatch_action("reload"));
    CHECK(fixture.host.init_error().empty());
    CHECK(fixture.host.status_text().find(
              "replacement-feature.md")
        != std::string::npos);
}

TEST_CASE("kanban host opens selected card in a Neovim host", "[kanban][host]")
{
    KanbanHostFixture fixture;

    fixture.host.on_key(key_event(SDLK_RETURN));

    constexpr std::string_view prefix = "open_file:";
    REQUIRE(fixture.callbacks.nvim_action.starts_with(prefix));
    const std::string opened_path = fixture.callbacks.nvim_action.substr(prefix.size());
    REQUIRE(std::filesystem::weakly_canonical(opened_path)
        == std::filesystem::weakly_canonical(fixture.card_path));

    // Enter surfaces the card in Neovim but must NOT steal focus from the board.
    REQUIRE(fixture.callbacks.nvim_keep_focus);
}

TEST_CASE("kanban host reports toast when Neovim open fails", "[kanban][host]")
{
    KanbanHostFixture fixture;
    fixture.callbacks.dispatch_nvim_result = false;

    fixture.host.on_key(key_event(SDLK_RETURN));

    REQUIRE(fixture.callbacks.toast_level == 2);
    REQUIRE(fixture.callbacks.toast_message.find("card-1-feature.md") != std::string::npos);
}

TEST_CASE("kanban host toggles a Markdown preview pane with p", "[kanban][host][input]")
{
    KanbanHostFixture fixture;

    // First 'p' opens the preview and points it at the selected card.
    fixture.host.on_key(key_event(SDLK_P));
    REQUIRE(fixture.callbacks.show_preview_calls == 1);
    REQUIRE(fixture.callbacks.preview_visible);
    REQUIRE(std::filesystem::weakly_canonical(fixture.callbacks.preview_path)
        == std::filesystem::weakly_canonical(fixture.card_path));

    // Second 'p' tears it back down.
    fixture.host.on_key(key_event(SDLK_P));
    REQUIRE(fixture.callbacks.hide_preview_calls == 1);
    REQUIRE_FALSE(fixture.callbacks.preview_visible);
}

TEST_CASE("kanban host preview follows the moved-to card", "[kanban][host][input]")
{
    KanbanHostFixture fixture(3);
    const auto second_card = fixture.temp.path / "todo" / "card-2-feature.md";

    fixture.host.on_key(key_event(SDLK_P));
    REQUIRE(fixture.callbacks.show_preview_calls == 1);

    // Moving the selection while the preview is pinned reloads it in place.
    fixture.host.on_key(key_event(SDLK_J));
    REQUIRE(fixture.callbacks.show_preview_calls == 2);
    REQUIRE(fixture.callbacks.hide_preview_calls == 0);
    REQUIRE(std::filesystem::weakly_canonical(fixture.callbacks.preview_path)
        == std::filesystem::weakly_canonical(second_card));
}

TEST_CASE("kanban host recognizes a preview restored by shared topology",
    "[kanban][host][input][restore]")
{
    KanbanHostFixture fixture;
    fixture.callbacks.preview_visible = true;

    fixture.host.on_key(key_event(SDLK_P));

    CHECK(fixture.callbacks.show_preview_calls == 0);
    CHECK(fixture.callbacks.hide_preview_calls == 1);
    CHECK_FALSE(fixture.callbacks.preview_visible);
}

TEST_CASE("kanban host does not touch the preview until it is pinned", "[kanban][host][input]")
{
    KanbanHostFixture fixture(3);

    // Navigating without a preview open must not spawn one.
    fixture.host.on_key(key_event(SDLK_J));
    REQUIRE(fixture.callbacks.show_preview_calls == 0);
}

namespace
{
// Widest column touched by cells carrying the "selected card" background on a
// given grid row, across every recorded update batch (later writes win).
int max_selected_col_on_row(const draxul::tests::FakeGridHandle& handle, int row)
{
    const Color selected_bg = color_from_rgb(0x3E5167);
    int max_col = -1;
    for (const auto& batch : handle.update_batches)
        for (const auto& cell : batch)
            if (cell.row == row && cell.bg == selected_bg)
                max_col = std::max(max_col, cell.col);
    return max_col;
}
} // namespace

TEST_CASE("kanban host zoom widens the selected column to full width", "[kanban][host][input]")
{
    KanbanHostFixture fixture(1, 2, { 80, 12 });
    REQUIRE(fixture.renderer.last_handle != nullptr);
    constexpr int kFirstCardRow = 3;

    // Zoom: the selected column (0) fills the whole 80-cell width, so its
    // selected card row extends well past the un-zoomed half-width column.
    fixture.renderer.last_handle->update_batches.clear();
    fixture.host.on_key(key_event(SDLK_Z));
    fixture.host.pump();
    const int zoomed_span = max_selected_col_on_row(*fixture.renderer.last_handle, kFirstCardRow);
    REQUIRE(zoomed_span > 60);

    // Un-zoom: back to side-by-side columns, so the selected span shrinks to
    // roughly the left half.
    fixture.renderer.last_handle->update_batches.clear();
    fixture.host.on_key(key_event(SDLK_Z));
    fixture.host.pump();
    const int unzoomed_span = max_selected_col_on_row(*fixture.renderer.last_handle, kFirstCardRow);
    REQUIRE(unzoomed_span > 0);
    REQUIRE(unzoomed_span < 45);
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

TEST_CASE("kanban host supports vim-style page and edge jumps within a column", "[kanban][host][input]")
{
    KanbanHostFixture fixture(20, 1, { 80, 12 }, true, true);

    fixture.host.on_key(key_event(SDLK_F, kModCtrl));
    REQUIRE(fixture.host.status_text().find("card-009-feature.md") != std::string::npos);

    fixture.host.on_key(key_event(SDLK_B, kModCtrl));
    REQUIRE(fixture.host.status_text().find("card-001-feature.md") != std::string::npos);

    fixture.host.on_key(key_event(SDLK_G, kModShift));
    REQUIRE(fixture.host.status_text().find("card-020-feature.md") != std::string::npos);

    fixture.host.on_key(key_event(SDLK_G));
    REQUIRE(fixture.host.status_text().find("card-020-feature.md") != std::string::npos);

    fixture.host.on_key(key_event(SDLK_G));
    REQUIRE(fixture.host.status_text().find("card-001-feature.md") != std::string::npos);
}

TEST_CASE("kanban host moves cards between columns with angle brackets only", "[kanban][host][input]")
{
    KanbanHostFixture fixture(1, 2, { 80, 12 }, false);
    const auto source_path = fixture.temp.path / "column-1" / "card-1-feature.md";
    const auto target_path = fixture.temp.path / "column-2" / "card-1-feature.md";

    fixture.host.on_key(key_event(SDLK_L, kModShift));
    REQUIRE(std::filesystem::exists(source_path));
    REQUIRE_FALSE(std::filesystem::exists(target_path));

    fixture.host.on_key(key_event(SDLK_PERIOD, kModShift));
    REQUIRE_FALSE(std::filesystem::exists(source_path));
    REQUIRE(std::filesystem::exists(target_path));
}

TEST_CASE("kanban host capital D deletes done and ice-box cards once per keypress",
    "[kanban][host][input][delete]")
{
    SECTION("done")
    {
        KanbanHostFixture fixture(2);
        const auto lane = fixture.temp.path / "done";
        std::filesystem::rename(fixture.temp.path / "todo", lane);
        REQUIRE(fixture.host.dispatch_action("reload"));

        const auto first = lane / "card-1-feature.md";
        const auto second = lane / "card-2-feature.md";
        fixture.host.on_key(key_event(SDLK_P));
        REQUIRE(std::filesystem::weakly_canonical(fixture.callbacks.preview_path)
            == std::filesystem::weakly_canonical(first));

        fixture.host.on_key(key_event(SDLK_D, kModShift));
        CHECK_FALSE(std::filesystem::exists(first));
        CHECK(std::filesystem::exists(second));
        CHECK(std::filesystem::weakly_canonical(fixture.callbacks.preview_path)
            == std::filesystem::weakly_canonical(second));

        fixture.host.on_key(key_event(SDLK_D, kModShift));
        CHECK(std::filesystem::exists(second));

        fixture.host.on_key(KeyEvent{
            .scancode = 0,
            .keycode = SDLK_D,
            .mod = kModShift,
            .pressed = false,
        });
        fixture.host.on_key(key_event(SDLK_D, kModShift));
        CHECK_FALSE(std::filesystem::exists(second));
        CHECK(fixture.host.status_text().find("0 cards") != std::string::npos);
        CHECK_FALSE(fixture.callbacks.preview_visible);
        CHECK(fixture.callbacks.hide_preview_calls == 1);
    }

    SECTION("ice-box")
    {
        KanbanHostFixture fixture;
        const auto lane = fixture.temp.path / "ice-box";
        std::filesystem::rename(fixture.temp.path / "todo", lane);
        REQUIRE(fixture.host.dispatch_action("reload"));

        const auto card = lane / "card-1-feature.md";
        fixture.host.on_key(key_event(SDLK_D, kModShift));
        CHECK_FALSE(std::filesystem::exists(card));
    }
}

TEST_CASE("kanban host capital D refuses pending and custom lanes",
    "[kanban][host][input][delete]")
{
    for (const std::string lane_name : { "pending", "review" })
    {
        DYNAMIC_SECTION(lane_name)
        {
            KanbanHostFixture fixture;
            const auto lane = fixture.temp.path / lane_name;
            std::filesystem::rename(fixture.temp.path / "todo", lane);
            REQUIRE(fixture.host.dispatch_action("reload"));

            const auto card = lane / "card-1-feature.md";
            fixture.host.on_key(key_event(SDLK_D, kModShift));
            CHECK(std::filesystem::exists(card));
            CHECK(fixture.callbacks.toast_level == 2);
            CHECK(fixture.callbacks.toast_message.find("done or ice-box")
                != std::string::npos);
        }
    }
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
    fixture.host.on_focus_gained();

    fixture.host.on_key(key_event(SDLK_J));
    REQUIRE(fixture.host.status_text().find("card-2-feature.md") != std::string::npos);

    std::this_thread::sleep_for(std::chrono::milliseconds(180));
    fixture.host.pump();

    REQUIRE(fixture.host.status_text().find("card-3-feature.md") != std::string::npos);
}

TEST_CASE("kanban host stops held-key repeat on key release", "[kanban][host][input]")
{
    KanbanHostFixture fixture(4);
    fixture.host.on_focus_gained();

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

TEST_CASE("kanban host cancels held navigation and preview updates when focus moves away",
    "[kanban][host][input]")
{
    KanbanHostFixture fixture(5);
    fixture.host.on_focus_gained();
    fixture.host.on_key(key_event(SDLK_P));
    fixture.host.on_key(key_event(SDLK_J));
    REQUIRE(fixture.host.status_text().find("card-2-feature.md") != std::string::npos);
    const int previews_after_key = fixture.callbacks.show_preview_calls;

    // The key-up is delivered to another tab, not to this host.
    fixture.host.on_focus_lost();
    std::this_thread::sleep_for(std::chrono::milliseconds(180));
    fixture.host.pump();
    CHECK(fixture.host.status_text().find("card-2-feature.md") != std::string::npos);
    CHECK(fixture.callbacks.show_preview_calls == previews_after_key);

    fixture.host.on_focus_gained();
    const int previews_after_refocus = fixture.callbacks.show_preview_calls;
    std::this_thread::sleep_for(std::chrono::milliseconds(180));
    fixture.host.pump();
    CHECK(fixture.host.status_text().find("card-2-feature.md") != std::string::npos);
    CHECK(fixture.callbacks.preview_path.find("card-2-feature.md") != std::string::npos);
    CHECK(fixture.callbacks.show_preview_calls == previews_after_refocus);

    fixture.host.on_key(key_event(SDLK_G));
    fixture.host.on_focus_lost();
    fixture.host.on_focus_gained();
    fixture.host.on_key(key_event(SDLK_G));
    CHECK(fixture.host.status_text().find("card-2-feature.md") != std::string::npos);

    fixture.host.on_key(key_event(SDLK_J));
    REQUIRE(fixture.host.status_text().find("card-3-feature.md") != std::string::npos);
    std::this_thread::sleep_for(std::chrono::milliseconds(180));
    fixture.host.pump();
    CHECK(fixture.host.status_text().find("card-4-feature.md") != std::string::npos);
    CHECK(fixture.callbacks.show_preview_calls > previews_after_key);
}
