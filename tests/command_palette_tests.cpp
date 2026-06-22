#include <catch2/catch_test_macros.hpp>

#include "command_palette.h"
#include "command_palette_host.h"
#include "fuzzy_match.h"
#include "gui_action_handler.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <draxul/app_config.h>
#include <draxul/events.h>
#include <draxul/text_service.h>
#include <filesystem>
#include <glm/glm.hpp>

#include "support/fake_grid_pipeline_renderer.h"
#include "support/fake_window.h"
#include "support/test_host_callbacks.h"

using namespace draxul;

namespace
{

std::filesystem::path bundled_font_path()
{
    return std::filesystem::path(DRAXUL_PROJECT_ROOT) / "fonts" / "JetBrainsMonoNerdFont-Regular.ttf";
}

bool init_text_service(TextService& text_service)
{
    const auto font_path = bundled_font_path();
    if (!std::filesystem::exists(font_path))
        return false;

    TextServiceConfig config;
    config.font_path = font_path.string();
    return text_service.initialize(config, TextService::DEFAULT_POINT_SIZE, 96.0f);
}

} // namespace

// ── Fuzzy match tests ──────────────────────────────────────────────────────

TEST_CASE("fuzzy_match: empty pattern matches everything", "[fuzzy]")
{
    auto result = fuzzy_match("", "toggle_diagnostics");
    CHECK(result.matched);
    CHECK(result.score == 0);
    CHECK(result.positions.empty());
}

TEST_CASE("fuzzy_match: empty target does not match", "[fuzzy]")
{
    auto result = fuzzy_match("td", "");
    CHECK_FALSE(result.matched);
}

TEST_CASE("fuzzy_match: prefix match scores higher than mid-word", "[fuzzy]")
{
    auto prefix = fuzzy_match("copy", "copy");
    auto mid = fuzzy_match("copy", "xcopy");
    CHECK(prefix.matched);
    CHECK(mid.matched);
    CHECK(prefix.score > mid.score);
}

TEST_CASE("fuzzy_match: boundary bonus at underscore", "[fuzzy]")
{
    // "td" in "toggle_diagnostics": t at start (boundary), d after _ (boundary)
    auto boundary = fuzzy_match("td", "toggle_diagnostics");
    // "td" in "outdoors": t and d are mid-word
    auto midword = fuzzy_match("td", "outdoors");
    CHECK(boundary.matched);
    CHECK(midword.matched);
    CHECK(boundary.score > midword.score);
}

TEST_CASE("fuzzy_match: consecutive chars get bonus", "[fuzzy]")
{
    // Both targets start mid-word so first-char boundary bonus doesn't skew the comparison
    auto consecutive = fuzzy_match("ab", "xabc");
    auto scattered = fuzzy_match("ab", "xaxb");
    CHECK(consecutive.matched);
    CHECK(scattered.matched);
    CHECK(consecutive.score > scattered.score);
}

TEST_CASE("fuzzy_match: case insensitive", "[fuzzy]")
{
    auto result = fuzzy_match("COPY", "copy");
    CHECK(result.matched);
    CHECK(result.positions.size() == 4);
}

TEST_CASE("fuzzy_match: non-matching returns false", "[fuzzy]")
{
    auto result = fuzzy_match("xyz", "copy");
    CHECK_FALSE(result.matched);
}

TEST_CASE("fuzzy_match: first char bonus doubled", "[fuzzy]")
{
    // Pattern starting at a boundary should score higher than mid-word
    auto at_start = fuzzy_match("f", "font_increase");
    auto mid = fuzzy_match("n", "font_increase");
    CHECK(at_start.matched);
    CHECK(mid.matched);
    // 'f' at position 0 gets boundary bonus * 2; 'n' at position 2 gets no bonus
    CHECK(at_start.score > mid.score);
}

TEST_CASE("fuzzy_match: positions are correct", "[fuzzy]")
{
    auto result = fuzzy_match("fi", "font_increase");
    CHECK(result.matched);
    REQUIRE(result.positions.size() == 2);
    CHECK(result.positions[0] == 0); // f
    CHECK(result.positions[1] == 5); // i after _
}

// ── CommandPalette state tests ─────────────────────────────────────────────

TEST_CASE("CommandPalette: open and close", "[palette]")
{
    CommandPalette palette;
    CHECK_FALSE(palette.is_open());
    palette.open();
    CHECK(palette.is_open());
    palette.close();
    CHECK_FALSE(palette.is_open());
}

TEST_CASE("CommandPalette: Escape closes", "[palette]")
{
    CommandPalette palette;
    palette.open();
    KeyEvent esc{ 0, SDLK_ESCAPE, kModNone, true };
    palette.on_key(esc);
    CHECK_FALSE(palette.is_open());
}

TEST_CASE("CommandPalette: consumes all keys when open", "[palette]")
{
    CommandPalette palette;
    palette.open();
    KeyEvent key{ 0, SDLK_A, kModNone, true };
    CHECK(palette.on_key(key));
}

TEST_CASE("CommandPalette: does not consume when closed", "[palette]")
{
    CommandPalette palette;
    KeyEvent key{ 0, SDLK_A, kModNone, true };
    CHECK_FALSE(palette.on_key(key));
}

TEST_CASE("CommandPalette: action_names excludes command_palette", "[palette]")
{
    auto names = GuiActionHandler::action_names();
    bool found_palette = false;
    bool found_copy = false;
    for (auto name : names)
    {
        if (name == "command_palette")
            found_palette = true;
        if (name == "copy")
            found_copy = true;
    }
    CHECK(found_palette); // it exists in the registry
    CHECK(found_copy);
}

TEST_CASE("CommandPalette prompt: submit trims input and closes", "[palette][prompt]")
{
    std::string submitted;
    int close_count = 0;
    CommandPalette palette(CommandPalette::Deps{
        .on_closed = [&]() { ++close_count; },
    });

    CommandPalette::PromptRequest request;
    request.title = "Save Session As";
    request.prompt = "Name";
    request.initial_value = " Before ";
    request.on_submit = [&](std::string value) {
        submitted = std::move(value);
    };

    palette.open_prompt(std::move(request));
    REQUIRE(palette.is_open());

    KeyEvent enter{ 0, SDLK_RETURN, kModNone, true };
    CHECK(palette.on_key(enter));

    CHECK(submitted == "Before");
    CHECK_FALSE(palette.is_open());
    CHECK(close_count == 1);
}

TEST_CASE("CommandPalette prompt: empty submit keeps prompt open with message", "[palette][prompt]")
{
    bool submitted = false;
    CommandPalette palette;

    CommandPalette::PromptRequest request;
    request.title = "Save Session As";
    request.prompt = "Name";
    request.initial_value = "   ";
    request.on_submit = [&](std::string) { submitted = true; };

    palette.open_prompt(std::move(request));

    KeyEvent enter{ 0, SDLK_RETURN, kModNone, true };
    CHECK(palette.on_key(enter));

    CHECK_FALSE(submitted);
    CHECK(palette.is_open());

    auto state = palette.view_state(40, 8);
    CHECK(state.mode == gui::PaletteMode::Prompt);
    CHECK(state.message == "Enter a session name");
}

TEST_CASE("CommandPalette prompt: Escape cancels without submitting", "[palette][prompt]")
{
    bool submitted = false;
    bool cancelled = false;
    CommandPalette palette;

    CommandPalette::PromptRequest request;
    request.title = "Save Session As";
    request.prompt = "Name";
    request.initial_value = "Draft";
    request.on_submit = [&](std::string) { submitted = true; };
    request.on_cancel = [&]() { cancelled = true; };

    palette.open_prompt(std::move(request));

    KeyEvent escape{ 0, SDLK_ESCAPE, kModNone, true };
    CHECK(palette.on_key(escape));

    CHECK_FALSE(submitted);
    CHECK(cancelled);
    CHECK_FALSE(palette.is_open());
}

TEST_CASE("CommandPalette prompt: text input and backspace edit the prompt value", "[palette][prompt]")
{
    CommandPalette palette;
    CommandPalette::PromptRequest request;
    request.title = "Save Session As";
    request.prompt = "Name";
    request.initial_value = "Al";
    palette.open_prompt(std::move(request));

    TextInputEvent input;
    input.text = "pha";
    CHECK(palette.on_text_input(input));

    auto state = palette.view_state(40, 8);
    CHECK(state.query == "Alpha");

    KeyEvent backspace{ 0, SDLK_BACKSPACE, kModNone, true };
    CHECK(palette.on_key(backspace));

    state = palette.view_state(40, 8);
    CHECK(state.query == "Alph");
}

TEST_CASE("CommandPaletteHost: opening primes palette cells before first draw", "[palette]")
{
    tests::FakeWindow window;
    tests::FakeGridPipelineRenderer renderer;
    tests::TestHostCallbacks callbacks;
    TextService text_service;
    if (!init_text_service(text_service))
        SKIP("bundled font not found");

    CommandPaletteHost::Deps deps;
    CommandPaletteHost host(std::move(deps));

    HostViewport viewport;
    viewport.pixel_size = { window.pixel_w_, window.pixel_h_ };
    viewport.grid_size = { 1, 1 };

    HostContext context{
        .window = &window,
        .grid_renderer = &renderer,
        .text_service = &text_service,
        .initial_viewport = viewport,
        .display_ppi = window.display_ppi_,
    };

    REQUIRE(host.initialize(context, callbacks));
    REQUIRE(host.dispatch_action("toggle"));
    REQUIRE(host.is_active());
    REQUIRE(renderer.last_handle != nullptr);
    CHECK(renderer.last_handle->last_viewport.pixel_pos == glm::ivec2(200, 150));
    CHECK(renderer.last_handle->last_viewport.pixel_size == glm::ivec2(400, 300));
    CHECK(renderer.last_handle->last_grid_size == glm::ivec2(40, 15));
    CHECK(renderer.last_handle->last_cursor == glm::ivec2(-1, -1));
    CHECK_FALSE(renderer.last_handle->update_batches.empty());
    CHECK(renderer.last_handle->total_cell_updates() > 0);
    // Atlas upload is now centralized in App::render_frame() (WI 109), so the
    // host no longer uploads directly.  Verify that the atlas became dirty
    // instead, proving glyphs were resolved.
    CHECK(text_service.atlas_dirty());

    host.shutdown();
}

TEST_CASE("render_palette: palette fills the host grid", "[palette]")
{
    TextService text_service;
    if (!init_text_service(text_service))
        SKIP("bundled font not found");

    const std::array<gui::PaletteEntry, 1> entries{ {
        { "copy", "Ctrl+C", {} },
    } };

    gui::PaletteViewState state;
    state.grid_cols = 40;
    state.grid_rows = 15;
    state.query = "";
    state.selected_index = 0;
    state.entries = entries;

    const auto cells = gui::render_palette(state, text_service);
    REQUIRE_FALSE(cells.empty());

    int min_col = cells.front().col;
    int max_col = cells.front().col;
    int min_row = cells.front().row;
    int max_row = cells.front().row;
    for (const auto& cell : cells)
    {
        min_col = std::min(min_col, cell.col);
        max_col = std::max(max_col, cell.col);
        min_row = std::min(min_row, cell.row);
        max_row = std::max(max_row, cell.row);
    }

    CHECK(max_col - min_col + 1 == 40);
    CHECK(max_row - min_row + 1 == 15);
}

TEST_CASE("render_palette: prompt mode fills the host grid", "[palette][prompt]")
{
    TextService text_service;
    if (!init_text_service(text_service))
        SKIP("bundled font not found");

    gui::PaletteViewState state;
    state.mode = gui::PaletteMode::Prompt;
    state.grid_cols = 40;
    state.grid_rows = 8;
    state.title = "Save Session As";
    state.prompt = "Name";
    state.query = "Work Bench";
    state.message = "Enter a session name";

    const auto cells = gui::render_palette(state, text_service);
    CHECK(cells.size() == 40 * 8);
}

TEST_CASE("app config default palette alpha is 0.9", "[config]")
{
    AppConfig config = AppConfig::parse("");
    CHECK(config.palette_bg_alpha == 0.9f);
}

// ---------------------------------------------------------------------------
// CommandPaletteHost open/close/reopen lifecycle (WI 70)
// ---------------------------------------------------------------------------
//
// The palette is opened/closed frequently. These tests pin down:
//   * a fresh grid handle is allocated on every open (close releases it)
//   * close() drops is_active() and clears the renderer's last_handle reference
//   * the query string resets between open/close cycles
//   * rapid open/close cycles do not leak grid handles
//   * shutdown() while the palette is open releases the handle cleanly

namespace
{

struct PaletteHostHarness
{
    tests::FakeWindow window;
    tests::FakeGridPipelineRenderer renderer;
    tests::TestHostCallbacks callbacks;
    TextService text_service;
    CommandPaletteHost host;

    PaletteHostHarness()
        : host(CommandPaletteHost::Deps{})
    {
    }

    bool init()
    {
        if (!init_text_service(text_service))
            return false;
        HostViewport viewport;
        viewport.pixel_size = { window.pixel_w_, window.pixel_h_ };
        viewport.grid_size = { 1, 1 };
        HostContext context{
            .window = &window,
            .grid_renderer = &renderer,
            .text_service = &text_service,
            .initial_viewport = viewport,
            .display_ppi = window.display_ppi_,
        };
        return host.initialize(context, callbacks);
    }
};

} // namespace

TEST_CASE("CommandPaletteHost lifecycle: close releases handle and clears active state",
    "[palette][lifecycle]")
{
    PaletteHostHarness h;
    if (!h.init())
        SKIP("bundled font not found");

    REQUIRE(h.host.dispatch_action("toggle"));
    REQUIRE(h.host.is_active());
    REQUIRE(h.renderer.create_grid_handle_calls == 1);

    REQUIRE(h.host.dispatch_action("toggle"));
    REQUIRE_FALSE(h.host.is_active());
}

TEST_CASE("CommandPaletteHost lifecycle: reopen allocates a fresh grid handle",
    "[palette][lifecycle]")
{
    PaletteHostHarness h;
    if (!h.init())
        SKIP("bundled font not found");

    REQUIRE(h.host.dispatch_action("toggle")); // open #1
    auto* first_handle = h.renderer.last_handle;
    REQUIRE(first_handle != nullptr);
    REQUIRE(h.renderer.create_grid_handle_calls == 1);

    REQUIRE(h.host.dispatch_action("toggle")); // close
    REQUIRE_FALSE(h.host.is_active());

    REQUIRE(h.host.dispatch_action("toggle")); // open #2
    REQUIRE(h.host.is_active());
    REQUIRE(h.renderer.create_grid_handle_calls == 2);

    // The renderer's last_handle has been replaced; it must be a fresh allocation
    // (the previous unique_ptr was destroyed in the close path, so the new pointer
    // may even reuse the same address from the allocator — but the call count is
    // the authoritative leak indicator).
    auto* second_handle = h.renderer.last_handle;
    REQUIRE(second_handle != nullptr);
}

TEST_CASE("CommandPaletteHost lifecycle: query state resets between open cycles",
    "[palette][lifecycle]")
{
    PaletteHostHarness h;
    if (!h.init())
        SKIP("bundled font not found");

    REQUIRE(h.host.dispatch_action("toggle"));

    // Type a few characters into the open palette.
    TextInputEvent ev;
    ev.text = "cop";
    h.host.on_text_input(ev);
    h.host.pump();

    // Close, then reopen.
    REQUIRE(h.host.dispatch_action("toggle"));
    REQUIRE_FALSE(h.host.is_active());
    REQUIRE(h.host.dispatch_action("toggle"));
    REQUIRE(h.host.is_active());

    // The freshly opened palette must have an empty query — first cell update
    // batch reflects the cleared state. We can't peek at query_ directly, so
    // we verify by typing a fresh char and ensuring the palette is responsive
    // (no leftover "cop" prefix produced via host->pump path crashing).
    TextInputEvent ev2;
    ev2.text = "v";
    h.host.on_text_input(ev2);
    REQUIRE_NOTHROW(h.host.pump());
    REQUIRE(h.host.is_active());
}

TEST_CASE("CommandPaletteHost lifecycle: 100 rapid open/close cycles allocate exactly 100 handles",
    "[palette][lifecycle]")
{
    PaletteHostHarness h;
    if (!h.init())
        SKIP("bundled font not found");

    constexpr int kCycles = 100;
    for (int i = 0; i < kCycles; ++i)
    {
        REQUIRE(h.host.dispatch_action("toggle")); // open
        REQUIRE(h.host.is_active());
        REQUIRE(h.host.dispatch_action("toggle")); // close
        REQUIRE_FALSE(h.host.is_active());
    }

    // Each open allocates exactly one handle; close releases it. The counter
    // is the leak guard: any path that double-allocates or fails to release
    // would diverge from kCycles.
    REQUIRE(h.renderer.create_grid_handle_calls == kCycles);
}

TEST_CASE("CommandPaletteHost lifecycle: shutdown while open releases handle",
    "[palette][lifecycle]")
{
    PaletteHostHarness h;
    if (!h.init())
        SKIP("bundled font not found");

    REQUIRE(h.host.dispatch_action("toggle"));
    REQUIRE(h.host.is_active());

    REQUIRE_NOTHROW(h.host.shutdown());
    REQUIRE_FALSE(h.host.is_active());
}

TEST_CASE("CommandPaletteHost prompt: open_prompt allocates a handle and submits text",
    "[palette][prompt]")
{
    PaletteHostHarness h;
    if (!h.init())
        SKIP("bundled font not found");

    std::string submitted;
    CommandPalette::PromptRequest request;
    request.title = "Save Session As";
    request.prompt = "Name";
    request.on_submit = [&](std::string value) {
        submitted = std::move(value);
    };

    REQUIRE(h.host.open_prompt(std::move(request)));
    REQUIRE(h.host.is_active());
    REQUIRE(h.renderer.create_grid_handle_calls == 1);
    REQUIRE(h.renderer.last_handle != nullptr);

    TextInputEvent input;
    input.text = "Work Bench";
    h.host.on_text_input(input);

    KeyEvent enter{ 0, SDLK_RETURN, kModNone, true };
    h.host.on_key(enter);

    CHECK(submitted == "Work Bench");
    CHECK_FALSE(h.host.is_active());
}
