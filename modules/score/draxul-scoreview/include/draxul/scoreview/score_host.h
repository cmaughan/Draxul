#pragma once

#include <draxul/host.h>
#include <draxul/nanovg_pass.h>
#include <draxul/notation/score_document.h>
#include <draxul/scoreview/layout_engine.h>
#include <draxul/scoreview/score_draw_list.h>

#include <memory>
#include <string>
#include <vector>

namespace draxul
{

class HostProviderRegistry;

namespace scoreview
{

// ScoreHost — music score viewer (plans/scoreview.md).
//
// With a --source file: Verovio lays the score out to fit the viewport width
// (phase 2), the SVG interpreter turns each page into a ScoreDrawList
// (phase 3), and draw() replays the visible pages through the shared NanoVG
// pass with vertical scrolling and zoom (phase 4). The semantic model is
// imported alongside for status metadata and future editing phases.
// Without a source, a placeholder grand-staff page is drawn.
class ScoreHost final : public draxul::IHost
{
public:
    ScoreHost() = default;

    bool initialize(const draxul::HostContext& context, draxul::IHostCallbacks& callbacks) override;
    void shutdown() override;
    bool is_running() const override;
    std::string init_error() const override
    {
        return init_error_;
    }

    void set_viewport(const draxul::HostViewport& viewport) override;
    void pump() override;
    void draw(draxul::IFrameContext& frame) override;
    std::optional<std::chrono::steady_clock::time_point> next_deadline() const override
    {
        return std::nullopt;
    }

    void on_key(const draxul::KeyEvent& event) override;
    void on_mouse_wheel(const draxul::MouseWheelEvent& event) override;

    bool dispatch_action(std::string_view action) override;
    void request_close() override;
    std::string status_text() const override;
    draxul::Color default_background() const override;
    draxul::HostRuntimeState runtime_state() const override;
    draxul::HostDebugState debug_state() const override;

private:
    float ui_scale() const;
    float page_margin() const;
    float page_gap() const;
    float content_height() const;
    float max_scroll() const;
    void scroll_by(float delta_px);
    void scroll_to(float scroll_px);
    void set_zoom(float zoom);
    int current_page() const;
    void relayout();

    std::unique_ptr<draxul::INanoVGPass> nanovg_pass_;
    draxul::HostViewport viewport_;
    draxul::IHostCallbacks* callbacks_ = nullptr;

    std::string source_path_;
    std::string init_error_;
    std::unique_ptr<ILayoutEngine> engine_;
    std::shared_ptr<const std::vector<ScoreDrawList>> pages_;
    draxul::notation::ScoreDocument model_;
    bool has_model_ = false;

    float zoom_ = 1.0f;
    float scroll_y_ = 0.0f;
    float page_width_px_ = 0.0f;
    float page_height_px_ = 0.0f;
    float page_scale_ = 0.0f;
    bool layout_dirty_ = true;
    bool running_ = false;
};

std::unique_ptr<draxul::IHost> create_score_host();
void register_score_host_provider(draxul::HostProviderRegistry& registry);

} // namespace scoreview
} // namespace draxul
