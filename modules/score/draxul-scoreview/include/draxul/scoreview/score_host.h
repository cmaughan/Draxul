#pragma once

#include <draxul/host.h>
#include <draxul/nanovg_pass.h>
#include <draxul/notation/score_document.h>
#include <draxul/scoreview/flow_controller.h>
#include <draxul/scoreview/keyboard_player_input.h>
#include <draxul/scoreview/layout_engine.h>
#include <draxul/scoreview/mic_player_input.h>
#include <draxul/scoreview/player_input.h>
#include <draxul/scoreview/score_draw_list.h>
#include <draxul/scoreview/score_highlight.h>

#include <chrono>
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
    void on_config_reloaded(const draxul::HostReloadConfig& config) override;
    void pump() override;
    void draw(draxul::IFrameContext& frame) override;
    std::optional<std::chrono::steady_clock::time_point> next_deadline() const override;

    void on_key(const draxul::KeyEvent& event) override;
    void on_mouse_wheel(const draxul::MouseWheelEvent& event) override;

    bool dispatch_action(std::string_view action) override;
    void request_close() override;
    std::string status_text() const override;
    draxul::Color default_background() const override;
    draxul::HostRuntimeState runtime_state() const override;
    draxul::HostDebugState debug_state() const override;
    draxul::HostPrintHint print_hint() const override;

private:
    enum class ViewMode : uint8_t
    {
        Paged, // the reading view: pages + vertical scroll
        Flow, // the conveyor: one strip, transport, note light-up
    };

    enum class GateInput : uint8_t
    {
        Keyboard, // dev piano row (scaffolding)
        Bot, // deterministic verification player
        Mic, // the acoustic listener (the product)
    };

    struct FlowBand
    {
        float target_h = 0.0f;
        float strip_y = 0.0f;
        float band_pad = 0.0f;
    };
    FlowBand flow_band() const; // shared by draw() and print_hint()

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
    void relayout_flow();
    void toggle_flow_mode();
    void apply_lit_update();
    void apply_verdict_update();
    int approx_measure() const;
    double now_seconds() const;
    void enter_gate_mode(GateInput input, double bot_pace_qpm, double bot_accuracy);
    void exit_gate_mode();
    // Swaps the player-input implementation without touching the session
    // (verdicts, score, transport survive). Falls back to the keyboard when
    // the microphone can't open; returns whether the requested input engaged.
    bool set_gate_input(GateInput input, double bot_pace_qpm, double bot_accuracy);
    bool handle_gate_key(int keycode);

    std::unique_ptr<draxul::INanoVGPass> nanovg_pass_;
    draxul::HostViewport viewport_;
    draxul::IHostCallbacks* callbacks_ = nullptr;

    std::string source_path_;
    std::string init_error_;
    std::unique_ptr<ILayoutEngine> engine_;
    std::shared_ptr<const std::vector<ScoreDrawList>> pages_;
    draxul::notation::ScoreDocument model_;
    bool has_model_ = false;

    // Window-clear / chrome-facing background; kept identical to the terminal
    // scheme (see default_background()). The score's own backdrop is a NanoVG
    // fill inside the pane.
    draxul::Color background_{ 0.08f, 0.09f, 0.10f, 1.0f };
    float zoom_ = 1.0f;
    float scroll_y_ = 0.0f;
    float page_width_px_ = 0.0f;
    float page_height_px_ = 0.0f;
    float page_scale_ = 0.0f;
    bool layout_dirty_ = true;
    bool running_ = false;

    // Conveyor state (plans/scoreview-conveyor.md). The strip is the whole
    // piece as one system; the controller owns transport/tempo/lit diffs and
    // the highlight overlay carries per-op flags for the renderer.
    ViewMode view_mode_ = ViewMode::Paged;
    bool flow_dirty_ = false;
    bool flow_autoplay_ = false;
    std::shared_ptr<const ScoreDrawList> strip_;
    FlowController flow_;
    ScoreHighlightState highlight_;
    std::chrono::steady_clock::time_point last_pump_{};

    // Gate state (plans/scoreview-gate.md). The input seam's production
    // implementation is the milestone-3 microphone listener; keyboard and
    // bot are scaffolding.
    std::unique_ptr<IPlayerInput> player_input_;
    KeyboardPlayerInput* keyboard_input_ = nullptr; // borrowed from player_input_
    MicPlayerInput* mic_input_ = nullptr; // borrowed from player_input_
    bool start_in_gate_ = false;
    GateInput gate_input_requested_ = GateInput::Keyboard;
    double gate_bot_accuracy_ = 1.0;
    std::chrono::steady_clock::time_point epoch_{};
    size_t last_logged_gate_ = 0;
    bool logged_gate_end_ = false;
};

std::unique_ptr<draxul::IHost> create_score_host();
void register_score_host_provider(draxul::HostProviderRegistry& registry);

} // namespace scoreview
} // namespace draxul
