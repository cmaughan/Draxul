// UiPanel <-> IImGuiHost attachment contract tests (kanban 29).
//
// Pins the attach/detach/shutdown ordering documented in
// <draxul/ui_panel.h> using a recording fake backend:
//   - attach initializes the backend against the panel's ImGui context
//   - detach (explicit, via shutdown(), or via destruction) shuts the
//     backend down exactly once per attachment, with the panel's context
//     current
//   - render() is the single frame-driving API: it begins a backend frame
//     and submits ImGui draw data through IFrameContext::render_imgui().

#include <draxul/base_renderer.h>
#include <draxul/imgui_host.h>
#include <draxul/ui_panel.h>

#include <catch2/catch_all.hpp>
#include <imgui.h>

#include <string>
#include <vector>

using namespace draxul;

namespace
{

// Records every IImGuiHost call into a shared log so ordering across
// multiple backends is observable. Mirrors a real backend just enough for
// UiPanel::render(): begin_imgui_frame() builds the font atlas the way
// ImGui_ImplMetal/Vulkan_NewFrame would.
class FakeImGuiBackend final : public IImGuiHost
{
public:
    FakeImGuiBackend(std::string name, std::vector<std::string>& log)
        : name_(std::move(name))
        , log_(log)
    {
    }

    bool initialize_imgui_backend() override
    {
        log_.push_back(name_ + ".initialize");
        context_at_initialize_ = ImGui::GetCurrentContext();
        return !fail_initialize;
    }

    void shutdown_imgui_backend() override
    {
        log_.push_back(name_ + ".shutdown");
        context_at_shutdown_ = ImGui::GetCurrentContext();
    }

    void rebuild_imgui_font_texture() override
    {
        log_.push_back(name_ + ".rebuild_fonts");
    }

    void begin_imgui_frame() override
    {
        log_.push_back(name_ + ".begin_frame");
        if (ImGui::GetCurrentContext())
        {
            unsigned char* pixels = nullptr;
            int width = 0;
            int height = 0;
            ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        }
    }

    int count(const std::string& suffix) const
    {
        int n = 0;
        for (const auto& entry : log_)
            if (entry == name_ + "." + suffix)
                ++n;
        return n;
    }

    bool fail_initialize = false;
    ImGuiContext* context_at_initialize_ = nullptr;
    ImGuiContext* context_at_shutdown_ = nullptr;

private:
    std::string name_;
    std::vector<std::string>& log_;
};

class FakePanelFrameContext final : public IFrameContext
{
public:
    void draw_grid_handle(IGridHandle&) override {}
    void record_render_pass(IRenderPass&, const RenderViewport&) override {}
    void render_imgui(const ImDrawData* draw_data, ImGuiContext* context) override
    {
        ++render_imgui_calls;
        last_draw_data = draw_data;
        last_context = context;
    }
    void flush_submit_chunk() override
    {
        ++flush_calls;
    }

    int render_imgui_calls = 0;
    int flush_calls = 0;
    const ImDrawData* last_draw_data = nullptr;
    ImGuiContext* last_context = nullptr;
};

// Gives the panel a visible layout with panel_height > 0 so render() does
// not early-out (1280x800 window, 10x20 cells — same shape as the
// ui_panel_layout_tests fixtures).
void make_panel_renderable(UiPanel& panel)
{
    panel.set_visible(true);
    panel.set_window_metrics(1280, 800, 10, 20, 1);
    REQUIRE(panel.layout().panel_height > 0);
}

} // namespace

TEST_CASE("ui panel backend: shutdown detaches and shuts the backend down exactly once", "[ui][imgui_backend]")
{
    std::vector<std::string> log;
    FakeImGuiBackend backend("metal", log);

    UiPanel panel;
    REQUIRE(panel.initialize());
    REQUIRE(panel.attach_imgui_backend(backend));

    INFO("attach initializes the backend against the panel's context");
    REQUIRE(backend.count("initialize") == 1);
    REQUIRE(backend.context_at_initialize_ != nullptr);

    panel.shutdown();

    INFO("shutdown() performs the implicit detach");
    REQUIRE(backend.count("shutdown") == 1);
    INFO("the panel's context is still alive and current while the backend shuts down");
    REQUIRE(backend.context_at_shutdown_ == backend.context_at_initialize_);

    INFO("a second shutdown() must not touch the detached backend");
    panel.shutdown();
    REQUIRE(backend.count("shutdown") == 1);
}

TEST_CASE("ui panel backend: explicit detach precedes shutdown without double shutdown", "[ui][imgui_backend]")
{
    std::vector<std::string> log;
    FakeImGuiBackend backend("metal", log);

    UiPanel panel;
    REQUIRE(panel.initialize());
    REQUIRE(panel.attach_imgui_backend(backend));

    panel.detach_imgui_backend();
    REQUIRE(backend.count("shutdown") == 1);
    REQUIRE(backend.context_at_shutdown_ == backend.context_at_initialize_);

    INFO("detach with nothing attached is a no-op");
    panel.detach_imgui_backend();
    REQUIRE(backend.count("shutdown") == 1);

    INFO("shutdown() after explicit detach must not call the backend again");
    panel.shutdown();
    REQUIRE(backend.count("shutdown") == 1);
}

TEST_CASE("ui panel backend: destructor detaches an attached backend", "[ui][imgui_backend]")
{
    std::vector<std::string> log;
    FakeImGuiBackend backend("metal", log);

    {
        UiPanel panel;
        REQUIRE(panel.initialize());
        REQUIRE(panel.attach_imgui_backend(backend));
    }

    REQUIRE(backend.count("shutdown") == 1);
    REQUIRE(backend.context_at_shutdown_ == backend.context_at_initialize_);
}

TEST_CASE("ui panel backend: attach before initialize stores nothing", "[ui][imgui_backend]")
{
    std::vector<std::string> log;
    FakeImGuiBackend backend("metal", log);

    UiPanel panel;
    INFO("attach requires initialize() to have succeeded");
    REQUIRE_FALSE(panel.attach_imgui_backend(backend));
    REQUIRE(backend.count("initialize") == 0);

    REQUIRE(panel.initialize());
    panel.shutdown();

    INFO("the rejected backend was never attached, so shutdown must not touch it");
    REQUIRE(backend.count("shutdown") == 0);
}

TEST_CASE("ui panel backend: failed backend initialization leaves nothing attached", "[ui][imgui_backend]")
{
    std::vector<std::string> log;
    FakeImGuiBackend backend("metal", log);
    backend.fail_initialize = true;

    UiPanel panel;
    REQUIRE(panel.initialize());
    REQUIRE_FALSE(panel.attach_imgui_backend(backend));
    REQUIRE(backend.count("initialize") == 1);

    INFO("set_font must not reach a backend whose initialization failed");
    panel.set_font("", 0.0f);
    REQUIRE(backend.count("rebuild_fonts") == 0);

    panel.shutdown();
    REQUIRE(backend.count("shutdown") == 0);
}

TEST_CASE("ui panel backend: attaching a new backend detaches the previous one first", "[ui][imgui_backend]")
{
    std::vector<std::string> log;
    FakeImGuiBackend first("first", log);
    FakeImGuiBackend second("second", log);

    UiPanel panel;
    REQUIRE(panel.initialize());
    REQUIRE(panel.attach_imgui_backend(first));

    INFO("re-attaching the current backend is an idempotent no-op");
    REQUIRE(panel.attach_imgui_backend(first));
    REQUIRE(first.count("initialize") == 1);

    REQUIRE(panel.attach_imgui_backend(second));

    const std::vector<std::string> expected = {
        "first.initialize",
        "first.shutdown",
        "second.initialize",
    };
    INFO("the old backend shuts down before the new one initializes");
    REQUIRE(log == expected);

    panel.shutdown();
    REQUIRE(first.count("shutdown") == 1);
    REQUIRE(second.count("shutdown") == 1);
}

TEST_CASE("ui panel backend: set_font rebuilds the font texture only while attached", "[ui][imgui_backend]")
{
    std::vector<std::string> log;
    FakeImGuiBackend backend("metal", log);

    UiPanel panel;
    REQUIRE(panel.initialize());

    panel.set_font("", 0.0f);
    REQUIRE(backend.count("rebuild_fonts") == 0);

    REQUIRE(panel.attach_imgui_backend(backend));
    panel.set_font("", 0.0f);
    REQUIRE(backend.count("rebuild_fonts") == 1);

    panel.detach_imgui_backend();
    panel.set_font("", 0.0f);
    REQUIRE(backend.count("rebuild_fonts") == 1);

    panel.shutdown();
}

TEST_CASE("ui panel backend: render drives one backend frame and submits draw data", "[ui][imgui_backend]")
{
    std::vector<std::string> log;
    FakeImGuiBackend backend("metal", log);
    FakePanelFrameContext frame;

    UiPanel panel;
    REQUIRE(panel.initialize());
    panel.set_font("", 0.0f); // default font so the atlas can build headless
    REQUIRE(panel.attach_imgui_backend(backend));
    make_panel_renderable(panel);

    panel.render(frame, 1.0f / 60.0f);

    INFO("render() begins exactly one backend frame");
    REQUIRE(backend.count("begin_frame") == 1);
    INFO("render() submits ImGui draw data through the frame context");
    REQUIRE(frame.render_imgui_calls == 1);
    REQUIRE(frame.last_draw_data != nullptr);
    REQUIRE(frame.last_context == backend.context_at_initialize_);

    panel.render(frame, 1.0f / 60.0f);
    REQUIRE(backend.count("begin_frame") == 2);
    REQUIRE(frame.render_imgui_calls == 2);

    panel.shutdown();
    REQUIRE(backend.count("shutdown") == 1);
}

TEST_CASE("ui panel backend: render is a no-op while hidden", "[ui][imgui_backend]")
{
    std::vector<std::string> log;
    FakeImGuiBackend backend("metal", log);
    FakePanelFrameContext frame;

    UiPanel panel;
    REQUIRE(panel.initialize());
    panel.set_font("", 0.0f);
    REQUIRE(panel.attach_imgui_backend(backend));

    panel.set_visible(false);
    panel.set_window_metrics(1280, 800, 10, 20, 1);
    panel.render(frame, 1.0f / 60.0f);

    REQUIRE(backend.count("begin_frame") == 0);
    REQUIRE(frame.render_imgui_calls == 0);

    panel.shutdown();
}
