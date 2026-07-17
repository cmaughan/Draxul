#pragma once

#include <draxul/host.h>
#include <draxul/nanovg_pass.h>
#include <draxul/notation/score_document.h>
#include <draxul/scoreview/analysis_overlay.h>
#include <draxul/scoreview/engraved_window.h>
#include <draxul/scoreview/flow_controller.h>
#include <draxul/scoreview/layout_engine.h>
#include <draxul/scoreview/piece_analysis.h>
#include <draxul/scoreview/player_input_rig.h>
#include <draxul/scoreview/player_model.h>
#include <draxul/scoreview/score_draw_list.h>
#include <draxul/scoreview/score_highlight.h>
#include <draxul/scoreview/window_engraver.h>

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

struct ImGuiContext;

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
class ScoreAudioController; // internal audio rig (src/score_audio_controller.h)
class ScoreSessionController; // internal player-memory session (src/score_session_controller.h)
class ScoreStreamController; // internal rolling-window stream (src/score_stream_controller.h)
class ScorePresentation; // internal frame composer (src/score_presentation.h)
struct ScoreViewModel; // per-frame inspector snapshot (src/score_view_model.h)
struct ScoreInspectorIntents; // deferred inspector mutations (src/score_view_model.h)

class ScoreHost final : public draxul::IHost
{
public:
    ScoreHost();
    ~ScoreHost() override;

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
    void on_mouse_button(const draxul::MouseButtonEvent& event) override;
    void on_mouse_move(const draxul::MouseMoveEvent& event) override;
    void on_text_input(const draxul::TextInputEvent& event) override;
    void attach_imgui_host(draxul::IImGuiHost& host) override;
    void set_imgui_font(const std::string& path, float size_pixels) override;

    bool dispatch_action(std::string_view action) override;
    void request_close() override;
    std::string status_text() const override;
    draxul::Color default_background() const override;
    draxul::HostRuntimeState runtime_state() const override;
    draxul::HostDebugState debug_state() const override;
    draxul::HostPrintHint print_hint() const override;

private:
    // Narrow friend seam for deterministic orchestration tests. Production
    // construction still owns the concrete WindowEngraver normally.
    friend class ScoreHostTestAccess;

    enum class ViewMode : uint8_t
    {
        Paged, // the reading view: pages + vertical scroll
        Flow, // the conveyor: one strip, transport, note light-up
    };

    enum class GateInput : uint8_t
    {
        Keyboard, // dev piano row (scaffolding)
        Bot, // deterministic verification player
        Mic, // the acoustic listener (the eventual product input)
        Midi, // a hardware MIDI keyboard — lossless ground truth for tuning
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
    void rebuild_analysis_overlay();
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
    bool rebuild_window(int first_bar, double stream_position_q, bool carry,
        bool preserve_tempo = false);
    // Swaps a freshly-engraved window into the live host state and replays the
    // carried transport/verdicts — the cheap half of a window advance, shared
    // by the synchronous rebuild and the async install.
    void install_window(EngravedWindow&& engraved, int first_bar, int count,
        double stream_offset_q, double stream_position_q, bool carry, bool preserve_tempo = false);
    void rebuild_highlight_from_palette();
    // Installs a finished background engrave if one is ready (called each pump).
    void poll_async_engrave();
    void handle_async_engrave_done(WindowEngraver::Done done);
    // The host-side half of a completed generation: fallback/view checks,
    // then the presentation install (flattened intent — the controller's
    // Completed type is internal).
    void apply_completed_engrave(WindowEngraver::Done done, double intent_position_q,
        bool carry, bool preserve_tempo, bool fallback_to_monolith);
    // Restarts the stream from bar 0 with a fresh program (verdicts and
    // composer plan dropped). keep_tempo preserves the tempo the player has
    // settled at — their learned pace is a property of the LEARNER, not of
    // the restart; only clearing the piece's record resets it (and the tempo
    // lock always wins either way).
    void restart_stream(bool keep_tempo);
    // C3 tempo ladder: cap the roll tempo at the entered bar's earned rung.
    void apply_tempo_ladder();
    // Drops the planned program AND the composer's slot-indexed policy state
    // together (they must never diverge), plus the plan-log cursor.
    void reset_stream_plan();
    // Re-engraves the current flow material after a spacing change, keeping
    // position and verdicts (streaming rebuilds the current window with
    // carry; the monolith goes through flow_dirty_). Resets the band scale.
    void reengrave_flow_in_place();
    double stream_position_q() const;
    // The click track: position-locked to the transport — ticks fire as the
    // playhead crosses beat lines, so gate mode falls silent while waiting.
    // Player memory (plans/scoreview-stream.md S0): outcomes drain into the
    // model each pump; the JSON progress file flushes at bar boundaries and
    // when the session ends.
    void begin_progress_session();
    void end_progress_session();
    // Wipes this piece's learning record (player model + progress file) and
    // restarts the stream from the top — the "clear progress" button.
    void clear_piece_progress();
    // Advances the rolling window when the playhead moves past its history
    // margin; records judged outcomes into the verdict archive first.
    void maybe_advance_stream();
    bool stream_active() const;
    void enter_gate_mode(GateInput input, double bot_pace_qpm, double bot_accuracy);
    void exit_gate_mode();
    // Swaps the player-input implementation without touching the session
    // (verdicts, score, transport survive). Falls back to the keyboard when
    // the microphone can't open; returns whether the requested input engaged.
    bool set_gate_input(
        GateInput input, double bot_pace_qpm, double bot_accuracy, int midi_port = -1);
    bool handle_gate_key(int keycode);
    // The ImGui debug/learning inspector: reads ONE per-frame snapshot and
    // requests every mutation through intents, applied at one defined point
    // after the frame is recorded (never mid-frame).
    void render_debug_ui(float dt);
    ScoreViewModel build_view_model() const;
    void apply_inspector_intents(const ScoreInspectorIntents& intents);

    std::unique_ptr<draxul::INanoVGPass> nanovg_pass_;
    draxul::HostViewport viewport_;
    draxul::IHostCallbacks* callbacks_ = nullptr;

    std::string source_path_;
    std::string init_error_;
    std::unique_ptr<ILayoutEngine> engine_;
    std::shared_ptr<const std::vector<ScoreDrawList>> pages_;
    // Green analysis annotations for the paged reading view (built alongside
    // pages_ in relayout; drawn only when the inspector toggle is on).
    std::shared_ptr<const AnalysisOverlay> analysis_overlay_;
    bool show_analysis_overlay_ = false; // 'a' in the paged view
    bool show_unique_chunks_ = false; // 's': ghost restated phrases
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

    // Gate state (plans/scoreview-gate.md). The rig owns the live input
    // implementation and the selection/fallback policy; the host talks to
    // the seam plus narrow device capabilities and never names a concrete
    // input type.
    PlayerInputRig input_rig_;
    bool start_in_gate_ = false;
    GateInput gate_input_requested_ = GateInput::Keyboard;
    // The MIDI port the inspector selected (index into the rig's
    // list_midi_ports at selection time); -1 = none chosen yet.
    int midi_port_requested_ = -1;
    // Which game the transport plays: Roll (the runner — default) or Gate
    // (wait mode, kept as a dev/verification instrument).
    FlowController::TransportMode game_mode_ = FlowController::TransportMode::Roll;
    double gate_bot_accuracy_ = 1.0;
    std::chrono::steady_clock::time_point epoch_{};
    size_t last_logged_gate_ = 0;
    bool logged_gate_end_ = false;

    // The rolling-window stream (kanban 21 ScoreStreamController): slicing,
    // the composer + program, the verdict archive, window bookkeeping, the
    // advance policy, and the async-engrave worker state machine. The host
    // keeps the sync engrave (main engine), the install swap, and the
    // view/transport checks. Internal component; never null.
    std::unique_ptr<ScoreStreamController> stream_;
    std::string source_bytes_;
    double piece_marking_qpm_ = 0.0;

    // The frame composer (kanban 21 ScorePresentation): NanoVG recording
    // for flow/paged/placeholder plus the fixed-sheet scale-lock cache.
    // Internal component; never null.
    std::unique_ptr<ScorePresentation> presentation_;
    // The score occupies a FIXED band — this fraction of the pane height —
    // and the sheet scales (locked) to fill it, so it never jumps or resizes
    // as the window scrolls. Adjustable in the debug UI.
    float score_height_frac_ = 0.40f;
    // Whole-piece note coloring: element id -> pairing-palette index for
    // every note in the current engraving, resolved once per window build
    // (spelling comes from the engine there). Every note wears its color on
    // the sheet always; the keyboard reuses these indices for its lit keys.
    std::unordered_map<std::string, int> note_palette_;

    // Waterfall (piano-roll) between the score and the keyboard: each note
    // falls as a colored block toward its key, block height = its duration in
    // beats, landing exactly as the transport crosses its onset (a visual
    // timing hint). Built from the timemap's on/off pairs per engraving.
    // WaterfallNote is defined in engraved_window.h so the background engraver
    // can produce the blocks off the main thread.
    std::vector<WaterfallNote> waterfall_notes_;
    bool show_waterfall_ = true;
    double waterfall_beats_ = 7.0; // beats of look-ahead the zone shows
    // Articulation: a played note holds for this fraction of its notated
    // length, so blocks and key-lights end early and successive notes get
    // daylight between them (there is always a subjective pause). ~0.95 is a
    // gentle legato; low values read as staccato.
    double note_gate_ = 0.95;

    // Wrong-note marking: a wrong note gets a small cross over its head (the
    // note keeps its spelling color); false leaves the sheet unmarked.
    bool mark_mistakes_ = true;
    // Sharp/flat notehead cue: true = the half-color-over-black split, false =
    // accidentals wear their full spelling color like the naturals.
    bool split_accidentals_ = true;
    // Composer: true = the adaptive stream (reviews, drills, simplification),
    // false = just scroll the original piece bar-by-bar, unchanged. OFF by
    // default while the adaptive program is tuned (over MIDI input); the
    // inspector checkbox or the `composer` launch token opts in.
    bool composer_enabled_ = false;
    // Spacing experiment (inspector-only): engrave the flow strip with note
    // space proportional to duration so the conveyor scrolls at near-constant
    // speed and score columns align with the waterfall. The paged reading
    // view always keeps authentic engraving spacing.
    bool proportional_spacing_ = false;
    // Spacing-debug slider overrides (inspector). Negative = follow the
    // preset above; set, they win over it. Applied on slider release (a
    // re-engrave costs ~100ms — never per drag frame).
    float spacing_linear_override_ = -1.0f;
    float spacing_non_linear_override_ = -1.0f;
    // Tempo lock: true = play at the piece's proper marking with no Roll-mode
    // adaptation (the runner won't ease the tempo from accuracy).
    bool lock_tempo_ = false;
    int ladder_bar_ = -1; // last bar the tempo ladder was applied for

    // Player memory + persistence (kanban 21 ScoreSessionController): the
    // per-piece model, progress file, session clock, flush policy, and the
    // cached piece analysis. Internal component; never null.
    std::unique_ptr<ScoreSessionController> session_;

    // The audio rig (kanban 21 ScoreAudioController): output stream,
    // metronome, audition, instrument voices, MIDI play-thru, soundfont
    // staging. Internal component — SDL-audio details never cross into the
    // host. Never null (constructed with the host).
    std::unique_ptr<ScoreAudioController> audio_;
    double quarters_per_bar_ = 4.0;

    // ImGui debug/learning inspector. Its own context (like the other 3D
    // hosts); a floating window drawn over the score, toggled with `\``.
    ImGuiContext* imgui_context_ = nullptr;
    draxul::IImGuiHost* imgui_backend_ = nullptr;
    std::string imgui_font_path_;
    float imgui_font_size_pixels_ = 13.0f;
    bool show_debug_ui_ = true;
    std::chrono::steady_clock::time_point last_imgui_time_{};
};

std::unique_ptr<draxul::IHost> create_score_host();
void register_score_host_provider(draxul::HostProviderRegistry& registry);

} // namespace scoreview
} // namespace draxul
