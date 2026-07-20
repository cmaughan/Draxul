#pragma once

// The per-frame view model + inspector intents (kanban 21, ScoreViewModel):
// everything the inspector READS, snapshotted once per frame (scalars by
// value; heavy aggregates by const pointer — they cannot change during the
// frame because every mutation flows through ScoreInspectorIntents, which
// the host applies at ONE defined point after the frame is recorded). The
// inspector never touches live host/controller state again — the mid-frame
// mutation class of bugs is structurally gone. Internal to
// draxul-scoreview-host (no public header).

#include <draxul/scoreview/piece_analysis.h>
#include <draxul/scoreview/player_input_rig.h>
#include <draxul/scoreview/player_model.h>
#include <draxul/scoreview/stream_program.h>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace draxul
{
namespace scoreview
{

struct ScoreViewModel
{
    // Transport
    bool playing = false;
    bool waiting = false;
    int transport_mode = 0; // FlowController::TransportMode value
    double position_q = 0.0;
    double stream_position_q = 0.0;
    double tempo_qpm = 0.0;
    double min_tempo_qpm = 0.0;
    double max_tempo_qpm = 0.0;
    double marking_qpm = 0.0;
    bool stream_roll_active = false; // rolling window + Roll mode
    bool engraving_busy = false;
    std::string composer_name; // active IComposer::name() (kanban 22 seam)

    // Performance
    double accuracy_ema = 0.0;
    int score = 0;
    int streak = 0;
    int miss_count = 0;
    int wrong_count = 0;

    // Input
    PlayerInputRig::Kind input_kind = PlayerInputRig::Kind::None;
    std::string midi_port_name;

    // View / spacing flags
    bool lock_tempo = false;
    bool show_waterfall = true;
    bool mark_mistakes = true;
    bool show_note_colors = true;
    bool split_accidentals = true;
    bool composer_enabled = false;
    bool composer_drills = false; // fabricated chord drills (opt-in)
    bool composer_scales = false; // scale fragments (opt-in)
    bool proportional_spacing = false;
    bool show_analysis_overlay = false; // green analysis annotations (paged view, 'a')
    bool show_unique_chunks = false; // ghost restated phrases (paged view, 's')
    float score_height_frac = 0.4f;
    double waterfall_beats = 7.0;
    double note_gate = 0.95;
    float spacing_linear_override = -1.0f;
    float spacing_non_linear_override = -1.0f;

    // Audio
    int tick_level = 0; // ScoreAudioController::TickLevel value
    bool audition = false;
    bool piano_voice = false;
    int loaded_soundfont_index = -1;
    int selected_soundfont_index = -1;
    const std::vector<std::filesystem::path>* soundfonts = nullptr;

    // Learning aggregates (stable for the frame — see header comment)
    const PlayerModel* model = nullptr;
    const PieceProfile* profile = nullptr;
    const StreamProgram* program = nullptr;
    bool composing = false;
    int bar_count = 0;
};

// Everything the inspector may ask the host to change, applied after the
// frame is recorded. Optionals carry new values; bools are one-shot actions.
struct ScoreInspectorIntents
{
    bool toggle_play = false;
    bool rewind_or_restart = false;
    std::optional<double> tempo_qpm;
    std::optional<bool> lock_tempo;

    bool select_keyboard_input = false;
    bool select_mic_input = false;
    std::optional<int> select_midi_port;

    std::optional<float> score_height_frac;
    std::optional<bool> show_waterfall;
    std::optional<double> waterfall_beats;
    std::optional<double> note_gate;
    std::optional<bool> mark_mistakes;
    std::optional<bool> show_note_colors;
    std::optional<bool> split_accidentals;
    std::optional<bool> composer_enabled;
    std::optional<bool> composer_drills;
    std::optional<bool> composer_scales;
    std::optional<bool> proportional_spacing;
    std::optional<bool> show_analysis_overlay;
    std::optional<bool> show_unique_chunks;
    std::optional<float> spacing_linear_override;
    std::optional<float> spacing_non_linear_override;
    bool reset_spacing_overrides = false;

    std::optional<int> tick_level;
    bool use_synth_voice = false;
    std::optional<int> use_piano_index;
    std::optional<bool> audition;

    bool clear_progress = false;

    bool any() const
    {
        return toggle_play || rewind_or_restart || tempo_qpm || lock_tempo
            || select_keyboard_input || select_mic_input || select_midi_port
            || score_height_frac || show_waterfall || waterfall_beats || note_gate
            || mark_mistakes || show_note_colors || split_accidentals || composer_enabled
            || composer_drills || composer_scales || proportional_spacing
            || show_analysis_overlay || show_unique_chunks
            || spacing_linear_override || spacing_non_linear_override
            || reset_spacing_overrides || tick_level || use_synth_voice || use_piano_index
            || audition || clear_progress;
    }
};

} // namespace scoreview
} // namespace draxul
