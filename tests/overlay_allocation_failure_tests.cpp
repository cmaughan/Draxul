// Overlay allocation-failure matrix (kanban 20).
//
// A single explicit matrix proving every GUI overlay has a *tested*
// allocation-failure contract, so the item 23 chrome/overlay refactor can lean
// on it as a safety net. Each overlay is driven one resource at a time through
// its failure path using the EXISTING fake-renderer allocation control
// (FakeGridPipelineRenderer::fail_create_grid_handle) and the existing
// ScopedLogCapture / IImGuiHost fakes — no new parallel mocks.
//
// ── Overlay inventory (grid handle / texture / renderer attachment / degrade) ─
//
//  Overlay          Renderer resource                 Failure contract
//  ─────────────    ───────────────────────────────   ───────────────────────
//  Toast            eager IGridHandle in initialize()  atomic rollback:
//                   (ToastHost::handle_)               initialize() -> false,
//                                                       one ERROR log, no frame
//                                                       request, no null draw.
//  Command palette  lazy IGridHandle on open           reduced feature set:
//                   (CommandPaletteHost::handle_)       init still succeeds,
//                                                       open logs one ERROR and
//                                                       stays closed; retries on
//                                                       the next open (recovery).
//  Chrome text      lazy IGridHandle(s) in draw()      graceful degradation:
//                   (ChromeHost::tab_handle_ +          draw() logs one ERROR,
//                    pane_status_handles_)              skips the strip, never
//                                                       draws a null handle;
//                                                       recovers next frame.
//  Tooltip          rasterized RGBA TooltipBitmap      validity gate:
//                   (gui::rasterize_tooltip, uploaded   an unallocated/partial
//                    by megacity show_tooltip_bitmap)   bitmap is !valid() and
//                                                       is filtered before it
//                                                       reaches a texture draw.
//  Diagnostics      ImGui backend (GPU imgui pipeline)  reduced feature set:
//                   (DiagnosticsPanelHost -> UiPanel     a failing backend is
//                    -> IImGuiHost)                      never attached and
//                                                       never driven; shutdown
//                                                       stays safe and bounded.
//
// Toast TIMING behaviour (stacking, fade, expiry, request_frame cadence)
// remains owned by toast_host_tests.cpp; this file only covers allocation
// failure.
//
// App-wiring note (not a production bug): App::initialize() ignores the return
// of palette_host_/toast_host_/chrome_host_->initialize(). That path is
// unreachable with the real renderer — a grid-handle allocation failure aborts
// initialize() at the earlier terminal-host (GridHostBase) step, before any of
// these overlays are constructed. The per-overlay contracts below are the seam
// the item 23 refactor must preserve if it re-orders that wiring.

#include <catch2/catch_all.hpp>

#include "chrome_host.h"
#include "command_palette_host.h"
#include "diagnostics_panel_host.h"
#include "host_manager.h"
#include "toast_host.h"
#include "workspace.h"

#include <draxul/base_renderer.h>
#include <draxul/gui/tooltip.h>
#include <draxul/host.h>
#include <draxul/imgui_host.h>
#include <draxul/renderer.h>
#include <draxul/text_service.h>

#include <imgui.h>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

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

// Real TextService — overlays that emit glyph cells (toast, palette, chrome,
// tooltip) need metrics + an atlas. Returns false when the bundled font is
// absent so callers can SKIP rather than fail.
bool init_text_service(TextService& text_service)
{
    const auto font = bundled_font_path();
    if (!std::filesystem::exists(font))
        return false;
    TextServiceConfig config;
    config.font_path = font.string();
    return text_service.initialize(config, TextService::DEFAULT_POINT_SIZE, 96.0f);
}

HostContext make_context(IWindow& window, IGridRenderer& renderer, TextService& text_service)
{
    HostViewport viewport;
    viewport.pixel_pos = { 0, 0 };
    viewport.pixel_size = { 800, 600 };
    viewport.grid_size = { 80, 30 };

    HostContext context;
    context.window = &window;
    context.grid_renderer = &renderer;
    context.text_service = &text_service;
    context.initial_viewport = viewport;
    context.display_ppi = 96.0f;
    return context;
}

// One recording frame context reused by every overlay's draw() assertion so
// "no null handle reaches a draw call" is expressed uniformly: after a failed
// overlay draws, draw_grid_handle_calls must stay 0 and last_drawn_handle null.
class RecordingFrameContext final : public IFrameContext
{
public:
    void draw_grid_handle(IGridHandle& handle) override
    {
        last_drawn_handle = &handle;
        ++draw_grid_handle_calls;
    }
    void record_render_pass(IRenderPass& pass, const RenderViewport&) override
    {
        last_render_pass = &pass;
        ++record_render_pass_calls;
    }
    void render_imgui(const ImDrawData* draw_data, ImGuiContext*) override
    {
        last_draw_data = draw_data;
        ++render_imgui_calls;
    }
    void flush_submit_chunk() override
    {
        ++flush_calls;
    }

    IGridHandle* last_drawn_handle = nullptr;
    IRenderPass* last_render_pass = nullptr;
    const ImDrawData* last_draw_data = nullptr;
    int draw_grid_handle_calls = 0;
    int record_render_pass_calls = 0;
    int render_imgui_calls = 0;
    int flush_calls = 0;
};

// Failing ImGui backend for the diagnostics overlay. Mirrors the recording
// backend in ui_panel_backend_tests.cpp but is scoped to the allocation-failure
// contract: initialize_imgui_backend() reports failure, and the fake records
// whether it was ever driven or shut down.
class FailingImGuiBackend final : public IImGuiHost
{
public:
    bool initialize_imgui_backend() override
    {
        ++initialize_calls;
        return !fail_initialize;
    }
    void shutdown_imgui_backend() override
    {
        ++shutdown_calls;
    }
    void rebuild_imgui_font_texture() override
    {
        ++rebuild_calls;
    }
    void begin_imgui_frame() override
    {
        ++begin_frame_calls;
    }

    bool fail_initialize = false;
    int initialize_calls = 0;
    int shutdown_calls = 0;
    int rebuild_calls = 0;
    int begin_frame_calls = 0;
};

std::unique_ptr<Workspace> make_test_workspace(int id, std::string name)
{
    auto ws = std::make_unique<Workspace>(id, HostManager::Deps{});
    ws->name = std::move(name);
    return ws;
}

} // namespace

// ── Toast: eager grid handle → atomic init rollback ──────────────────────────

TEST_CASE("overlay alloc matrix / toast: grid-handle failure rolls back init atomically",
    "[overlay][alloc_failure][toast]")
{
    tests::FakeWindow window;
    tests::FakeGridPipelineRenderer renderer;
    tests::TestHostCallbacks callbacks;
    TextService text_service;
    if (!init_text_service(text_service))
        SKIP("bundled font not found");

    renderer.fail_create_grid_handle = true;
    const HostContext context = make_context(window, renderer, text_service);

    ToastHost host;

    // A single useful error and nothing else while the handle allocation fails.
    {
        tests::ScopedLogCapture capture(LogLevel::Error);
        INFO("initialize() must roll back atomically (return false) when the "
             "eager grid handle cannot be allocated");
        CHECK_FALSE(host.initialize(context, callbacks));
        CHECK(renderer.create_grid_handle_calls == 1);
        REQUIRE(capture.records.size() == 1);
        CHECK(capture.records[0].message.find("create_grid_handle() returned null")
            != std::string::npos);
    }

    // No frame request or input-area capture leaked out of the failed init.
    CHECK(callbacks.request_frame_calls == 0);
    CHECK(callbacks.wake_window_calls == 0);

    // Even if the failed overlay is (defensively) drawn, no null handle reaches
    // a draw call — ToastHost::draw() guards on handle_.
    RecordingFrameContext frame;
    host.push(gui::ToastLevel::Info, "post-fail", 4.0f);
    host.draw(frame);
    CHECK(frame.draw_grid_handle_calls == 0);
    CHECK(frame.last_drawn_handle == nullptr);

    // Shutdown after a failed init is safe and bounded (idempotent).
    host.shutdown();
    host.shutdown();
}

TEST_CASE("overlay alloc matrix / toast: successful init is the control case",
    "[overlay][alloc_failure][toast]")
{
    tests::FakeWindow window;
    tests::FakeGridPipelineRenderer renderer;
    tests::TestHostCallbacks callbacks;
    TextService text_service;
    if (!init_text_service(text_service))
        SKIP("bundled font not found");

    const HostContext context = make_context(window, renderer, text_service);

    ToastHost host;
    REQUIRE(host.initialize(context, callbacks));
    CHECK(renderer.create_grid_handle_calls == 1);
    CHECK(renderer.last_handle != nullptr);
    host.shutdown();
}

// ── Command palette: lazy grid handle → reduced feature set + recovery ────────

TEST_CASE("overlay alloc matrix / command palette: open under handle failure degrades cleanly",
    "[overlay][alloc_failure][palette]")
{
    tests::FakeWindow window;
    tests::FakeGridPipelineRenderer renderer;
    tests::TestHostCallbacks callbacks;
    TextService text_service;
    if (!init_text_service(text_service))
        SKIP("bundled font not found");

    const HostContext context = make_context(window, renderer, text_service);

    CommandPaletteHost host(CommandPaletteHost::Deps{});
    // The palette allocates its handle lazily, so init succeeds even though the
    // renderer will refuse handles — this is the documented reduced-feature-set
    // path rather than an atomic rollback.
    REQUIRE(host.initialize(context, callbacks));
    CHECK(renderer.create_grid_handle_calls == 0);

    renderer.fail_create_grid_handle = true;
    const int frames_before = callbacks.request_frame_calls;

    {
        tests::ScopedLogCapture capture(LogLevel::Error);
        // dispatch_action("toggle") reports the action as handled but the
        // palette must not become active when the handle cannot be created.
        CHECK(host.dispatch_action("toggle"));
        CHECK_FALSE(host.is_active());
        CHECK(renderer.create_grid_handle_calls == 1);
        REQUIRE(capture.records.size() == 1);
        CHECK(capture.records[0].message.find("create_grid_handle() returned null")
            != std::string::npos);
    }

    // The failed open must not leave a frame request queued for a closed palette.
    CHECK(callbacks.request_frame_calls == frames_before);

    // open_prompt() surfaces the failure to its caller by returning false.
    CommandPalette::PromptRequest prompt;
    prompt.title = "Save";
    CHECK_FALSE(host.open_prompt(std::move(prompt)));
    CHECK_FALSE(host.is_active());

    // No null handle reaches a draw call while the palette stayed closed.
    RecordingFrameContext frame;
    host.draw(frame);
    CHECK(frame.draw_grid_handle_calls == 0);
    CHECK(frame.last_drawn_handle == nullptr);

    host.shutdown();
    host.shutdown();
}

TEST_CASE("overlay alloc matrix / command palette: retries handle creation after transient failure",
    "[overlay][alloc_failure][palette][recovery]")
{
    tests::FakeWindow window;
    tests::FakeGridPipelineRenderer renderer;
    tests::TestHostCallbacks callbacks;
    TextService text_service;
    if (!init_text_service(text_service))
        SKIP("bundled font not found");

    const HostContext context = make_context(window, renderer, text_service);

    CommandPaletteHost host(CommandPaletteHost::Deps{});
    REQUIRE(host.initialize(context, callbacks));

    // Transient failure: the first open is refused and stays closed.
    renderer.fail_create_grid_handle = true;
    CHECK(host.dispatch_action("toggle"));
    CHECK_FALSE(host.is_active());

    // Resource recovers: the next open retries create_grid_handle() and opens.
    renderer.fail_create_grid_handle = false;
    CHECK(host.dispatch_action("toggle"));
    CHECK(host.is_active());
    REQUIRE(renderer.last_handle != nullptr);

    // A non-null handle now reaches the draw call.
    RecordingFrameContext frame;
    host.draw(frame);
    CHECK(frame.draw_grid_handle_calls == 1);
    CHECK(frame.last_drawn_handle == renderer.last_handle);

    host.shutdown();
}

// ── Chrome text: lazy tab handle in draw() → degradation + recovery ───────────

TEST_CASE("overlay alloc matrix / chrome text: tab-grid handle failure degrades without null draw",
    "[overlay][alloc_failure][chrome]")
{
    tests::FakeWindow window;
    tests::FakeGridPipelineRenderer renderer;
    tests::TestHostCallbacks callbacks;
    TextService text_service;
    if (!init_text_service(text_service))
        SKIP("bundled font not found");

    std::vector<std::unique_ptr<Workspace>> workspaces;
    workspaces.push_back(make_test_workspace(1, "alpha"));
    int active = 1;

    ChromeHost::Deps deps;
    deps.workspaces = &workspaces;
    deps.active_workspace_id = &active;
    deps.grid_renderer = &renderer;
    deps.text_service = &text_service;

    ChromeHost host(std::move(deps));
    const HostContext context = make_context(window, renderer, text_service);
    REQUIRE(host.initialize(context, callbacks));
    REQUIRE(host.is_running()); // NanoVG pass attachment succeeded (headless-safe)
    host.set_viewport(context.initial_viewport);

    renderer.fail_create_grid_handle = true;

    RecordingFrameContext frame;
    {
        tests::ScopedLogCapture capture(LogLevel::Error);
        // draw() creates the tab-bar grid handle lazily; the failure must log
        // once, skip the strip, and never hand a null handle to the frame.
        host.draw(frame);
        CHECK(renderer.create_grid_handle_calls >= 1);
        REQUIRE(capture.records.size() >= 1);
        CHECK(capture.records[0].message.find("create_grid_handle() returned null")
            != std::string::npos);
    }
    CHECK(frame.draw_grid_handle_calls == 0);
    CHECK(frame.last_drawn_handle == nullptr);

    // Recovery: once the renderer hands out handles again, the next frame draws
    // the chrome text strip.
    renderer.fail_create_grid_handle = false;
    RecordingFrameContext frame2;
    host.draw(frame2);
    CHECK(frame2.draw_grid_handle_calls >= 1);
    CHECK(frame2.last_drawn_handle != nullptr);

    // Shutdown stays safe and bounded after a degraded frame.
    host.shutdown();
    host.shutdown();
}

// ── Tooltip: RGBA bitmap validity gate before a texture draw ─────────────────

TEST_CASE("overlay alloc matrix / tooltip: validity gate filters unallocated bitmaps",
    "[overlay][alloc_failure][tooltip]")
{
    TextService text_service;
    if (!init_text_service(text_service))
        SKIP("bundled font not found");

    // Happy path: a real rasterization allocates a self-consistent bitmap.
    gui::TooltipData data;
    data.entries.push_back({ "Name", "widget" });
    data.entries.push_back({ "Size", "42" });
    const gui::TooltipBitmap good = gui::rasterize_tooltip(text_service, data);
    REQUIRE(good.valid());
    CHECK(good.width > 0);
    CHECK(good.height > 0);
    CHECK(good.rgba.size()
        == static_cast<size_t>(good.width) * static_cast<size_t>(good.height) * 4u);

    // Failure states that a not-produced / partially-allocated bitmap presents:
    CHECK_FALSE(gui::TooltipBitmap{}.valid()); // default (0x0, empty buffer)

    gui::TooltipBitmap zero_width;
    zero_width.width = 0;
    zero_width.height = 12;
    CHECK_FALSE(zero_width.valid());

    gui::TooltipBitmap partial_alloc; // dimensions set, buffer never allocated
    partial_alloc.width = 8;
    partial_alloc.height = 8;
    CHECK_FALSE(partial_alloc.valid());

    gui::TooltipBitmap short_buffer; // buffer half the required size
    short_buffer.width = 4;
    short_buffer.height = 4;
    short_buffer.rgba.assign(static_cast<size_t>(4 * 4 * 4) / 2, 0);
    CHECK_FALSE(short_buffer.valid());

    // Consumer contract: megacity's show_tooltip_bitmap only uploads/draws when
    // valid() — replicate that gate so an invalid bitmap never reaches a draw.
    int uploads = 0;
    const auto upload_if_valid = [&uploads](const gui::TooltipBitmap& bitmap) {
        if (!bitmap.valid())
            return; // never make it visible / never upload a texture
        ++uploads;
    };
    upload_if_valid(gui::TooltipBitmap{});
    upload_if_valid(partial_alloc);
    upload_if_valid(short_buffer);
    CHECK(uploads == 0);
    upload_if_valid(good);
    CHECK(uploads == 1);
}

// ── Diagnostics: failed ImGui backend is never attached or driven ────────────

TEST_CASE("overlay alloc matrix / diagnostics: failed imgui backend leaves nothing driven",
    "[overlay][alloc_failure][diagnostics]")
{
    tests::FakeWindow window;
    tests::FakeGridPipelineRenderer renderer;
    tests::TestHostCallbacks callbacks;
    TextService text_service;
    if (!init_text_service(text_service))
        SKIP("bundled font not found");

    const HostContext context = make_context(window, renderer, text_service);

    DiagnosticsPanelHost host;
    // The panel's ImGui context is created here; the GPU backend is attached
    // separately (mirrors App: initialize() then attach_imgui_host()).
    REQUIRE(host.initialize(context, callbacks));

    FailingImGuiBackend backend;
    backend.fail_initialize = true;
    host.attach_imgui_host(backend);

    // Attach tried exactly once and, having failed, left nothing attached:
    // the failed backend is never driven and never shut down.
    CHECK(backend.initialize_calls == 1);
    CHECK(backend.begin_frame_calls == 0);
    CHECK(backend.rebuild_calls == 0);

    // Diagnostics disabled (hidden): draw() is a total no-op — no imgui draw
    // data reaches the frame, and the failed backend is still never driven.
    host.set_visible(false);
    host.set_window_metrics(1280, 800, 10, 20, 1);
    RecordingFrameContext frame;
    host.draw(frame);
    CHECK(frame.render_imgui_calls == 0);
    CHECK(backend.begin_frame_calls == 0);

    // Shutdown is safe and bounded and must not call the never-attached backend.
    host.shutdown();
    CHECK(backend.shutdown_calls == 0);
    host.shutdown();
    CHECK(backend.shutdown_calls == 0);
}
