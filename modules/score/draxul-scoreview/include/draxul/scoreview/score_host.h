#pragma once

#include <draxul/host.h>
#include <draxul/nanovg_pass.h>
#include <draxul/notation/score_document.h>
#include <draxul/scoreview/flow_controller.h>
#include <draxul/scoreview/keyboard_player_input.h>
#include <draxul/scoreview/layout_engine.h>
#include <draxul/scoreview/metronome_synth.h>
#include <draxul/scoreview/mic_player_input.h>
#include <draxul/scoreview/piece_analysis.h>
#include <draxul/scoreview/player_input.h>
#include <draxul/scoreview/player_model.h>
#include <draxul/scoreview/score_draw_list.h>
#include <draxul/scoreview/score_highlight.h>
#include <draxul/scoreview/source_slicer.h>

#include <chrono>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <utility>
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

    enum class TickLevel : uint8_t
    {
        Off,
        Beats, // quarters, bar downbeat accented
        Eighths, // beats + quieter subdivision ticks
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
    double quarters_per_measure_from_model() const;
    double now_seconds() const;
    // The rolling window (plans/scoreview-stream.md S2): the roll game runs
    // on a short re-engraved window of the stream; the transport's local
    // axis maps to the stream via stream_offset_q_.
    enum class FlowBuildResult : uint8_t
    {
        InterpretFailed,
        TransportFailed,
        Ok,
    };
    FlowBuildResult build_flow_from_engine(std::string& error);
    bool rebuild_window(int first_bar, double stream_position_q, bool carry);
    double stream_position_q() const
    {
        return flow_.position_q() + stream_offset_q_;
    }
    // The click track: position-locked to the transport — ticks fire as the
    // playhead crosses beat lines, so gate mode falls silent while waiting.
    void cycle_tick_level();
    bool ensure_tick_stream();
    void pump_metronome(double p0_q, double p1_q, double dt);
    // Player memory (plans/scoreview-stream.md S0): outcomes drain into the
    // model each pump; the JSON progress file flushes at bar boundaries and
    // when the session ends.
    void begin_progress_session();
    void end_progress_session();
    void save_progress(bool final_flush);
    // Advances the rolling window when the playhead moves past its history
    // margin; records judged outcomes into the verdict archive first.
    void maybe_advance_stream();
    bool stream_active() const
    {
        return engine_holds_window_;
    }
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
    // Which game the transport plays: Roll (the runner — default) or Gate
    // (wait mode, kept as a dev/verification instrument).
    FlowController::TransportMode game_mode_ = FlowController::TransportMode::Roll;
    double gate_bot_accuracy_ = 1.0;
    std::chrono::steady_clock::time_point epoch_{};
    size_t last_logged_gate_ = 0;
    bool logged_gate_end_ = false;

    // Rolling window state (S2). `mono` command falls back to the
    // monolithic strip (the equivalence-verification instrument).
    SourceSlicer slicer_;
    std::string source_bytes_;
    bool stream_windowed_ = true;
    bool engine_holds_window_ = false;
    int window_first_bar_ = 0;
    int window_bar_count_ = 0;
    double stream_offset_q_ = 0.0;
    double piece_marking_qpm_ = 0.0;
    // Verdicts already earned, keyed by (stream q in thousandths, pitch) —
    // re-applied to the fresh engraving after every window swap.
    std::map<std::pair<long long, int>, FlowController::NoteVerdict> verdict_archive_;
    static constexpr int kWindowHistoryBars = 1;
    static constexpr int kWindowAheadBars = 8;

    // Piece analysis (S1): computed at flow build, cached for the composer.
    PieceProfile piece_profile_;

    // Player memory (S0): per-piece aggregates + the progress file.
    PlayerModel player_model_;
    std::filesystem::path progress_path_;
    std::chrono::steady_clock::time_point session_start_{};
    int last_flush_bar_ = -1;
    bool progress_dirty_ = false;

    // Metronome (audible tick) state; the SDL playback stream opens lazily.
    // Default ON with subdivisions — the click is the runner's pace signal.
    TickLevel tick_level_ = TickLevel::Eighths;
    MetronomeSynth metronome_;
    struct SDL_AudioStream* tick_stream_ = nullptr;
    std::vector<float> tick_buffer_;
    double quarters_per_bar_ = 4.0;
};

std::unique_ptr<draxul::IHost> create_score_host();
void register_score_host_provider(draxul::HostProviderRegistry& registry);

} // namespace scoreview
} // namespace draxul
