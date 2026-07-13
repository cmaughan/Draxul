#pragma once

#include <draxul/scoreview/player_input.h>
#include <draxul/scoreview/score_draw_list.h>
#include <draxul/scoreview/score_timemap.h>

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace draxul
{
namespace scoreview
{

// Transport and geometry for the flowing single-row view (conveyor
// milestone, plans/scoreview-conveyor.md). Pure logic, unit-testable: joins
// a Timemap against the interpreted strip and turns wall-clock time into a
// quarter-note position, a scroll offset, and lit-note diffs.
//
// advance() is the deliberate seam for the manifesto's later milestones:
// today a steady clock advances the position; milestone 2 advances it from
// matched player input instead. Everything downstream is unchanged.
class FlowController
{
public:
    struct Onset
    {
        double qstamp = 0.0;
        float x = 0.0f; // strip canvas units
        std::vector<std::string> ids;
    };

    // What changed in the lit set since the last call. When reset is true the
    // caller clears its highlight state before applying newly_lit (emitted
    // after a rewind or backward seek, replayed from the start).
    struct LitUpdate
    {
        bool reset = false;
        std::vector<std::string> newly_lit;
    };

    // Clock is the milestone-1 conveyor. Gate (plans/scoreview-gate.md)
    // waits at each onset — kept as a dev/verification instrument. Roll
    // (plans/scoreview-runner.md) is the game: the transport never waits,
    // notes judge in a timing window around their crossing, and tempo
    // eases from demonstrated accuracy.
    enum class TransportMode : uint8_t
    {
        Clock,
        Gate,
        Roll,
    };

    enum class NoteVerdict : uint8_t
    {
        Pending,
        Correct,
        Missed,
    };

    struct GateNote
    {
        std::string id;
        int pitch = -1;
        // Tie continuations (and unjudgeable ids) are not required to open
        // the gate; playing them anyway counts correct (re-voiced tie).
        bool auto_satisfied = false;
        NoteVerdict verdict = NoteVerdict::Pending;
        double hit_delta_q = 0.0; // Roll: timing offset of the hit, beats
    };

    struct Gate
    {
        std::vector<GateNote> notes;
        int required = 0; // non-auto notes
        int events = 0; // player events applied (right or wrong)
        bool open = false;
    };

    struct VerdictUpdate
    {
        bool reset = false;
        std::vector<std::pair<std::string, NoteVerdict>> changes;
    };

    // Joins timemap onsets with draw-op x positions. Returns false (and sets
    // `error`) when the join yields no usable onsets. Non-monotonic onset x
    // (score repeats revisiting earlier measures) is clamped to forward
    // motion for this milestone and counted in non_monotonic_count().
    bool build(const Timemap& timemap, const ScoreDrawList& strip, std::string& error);
    bool ready() const
    {
        return !onsets_.empty();
    }

    const std::vector<Onset>& onsets() const
    {
        return onsets_;
    }
    int join_miss_count() const
    {
        return join_miss_count_;
    }
    int non_monotonic_count() const
    {
        return non_monotonic_count_;
    }

    double duration_q() const
    {
        return duration_q_;
    }
    // The piece's tempo marking; kFallbackMarkingQpm when the piece has none.
    double marking_qpm() const
    {
        return marking_qpm_;
    }

    // Transport ---------------------------------------------------------
    void play();
    void pause();
    bool playing() const
    {
        return playing_;
    }
    void rewind();
    void seek(double qstamp);
    // Advances by wall-clock time at the current tempo; no-op while paused.
    // Auto-pauses when the end of the piece is reached.
    void advance(double wall_dt_seconds);
    double position_q() const
    {
        return position_q_;
    }
    bool at_end() const;

    // Tempo (quarter notes per minute) -----------------------------------
    // Clamped to [marking * kMinTempoFrac, marking * kMaxTempoFrac] — the
    // manifesto's cap: never more than ~20% over the piece's marking.
    double tempo_qpm() const
    {
        return tempo_qpm_;
    }
    void set_tempo_qpm(double qpm);
    double min_tempo_qpm() const;
    double max_tempo_qpm() const;

    // Geometry (strip canvas units) --------------------------------------
    // Piecewise-linear x for a quarter-note position: engraved spacing makes
    // the conveyor's speed breathe with the notation.
    double x_at(double qstamp) const;
    // Scroll so the playhead (anchored anchor_frac from the viewport's left)
    // sits at x_at(position), clamped to the strip.
    double scroll_x(double viewport_w_canvas, double anchor_frac) const;
    double canvas_width() const
    {
        return canvas_width_;
    }

    // Highlight diffs -----------------------------------------------------
    LitUpdate take_lit_update();

    // Gate mode -----------------------------------------------------------
    // Resolves expected pitches per onset (same id space) and marks tie
    // continuations auto-satisfied. Requires a successful build().
    void prepare_gates(const std::function<int(const std::string&)>& pitch_for,
        const std::vector<std::string>& tie_end_ids);
    bool gates_ready() const
    {
        return !gates_.empty();
    }
    const std::vector<Gate>& gates() const
    {
        return gates_;
    }

    // Switching modes rewinds the transport (verdicts, score, position).
    void set_mode(TransportMode mode);
    TransportMode mode() const
    {
        return mode_;
    }

    // True when the playhead sits at the armed gate awaiting the player
    // (Gate mode only; Roll never waits).
    bool waiting() const;
    size_t armed_gate_index() const
    {
        return mode_ == TransportMode::Roll ? roll_resolved_ : armed_;
    }
    // Still-pending required pitches the player should produce now: the
    // armed gate's in Gate mode; every onset whose hit window contains the
    // playhead in Roll mode (this is what primes the mic listener).
    std::vector<int> armed_required_pitches() const;

    // Applies player events. Gate mode: matching pitches turn Correct, any
    // event counts as an attempt, and the gate opens on full match or
    // enough attempts (remaining notes Missed — the music moves on). Roll
    // mode: an event hits the nearest onset within the timing window that
    // still needs its pitch; matching nothing counts as a wrong note.
    void judge(const std::vector<PlayerNoteEvent>& events);

    // Roll diagnostics: rolling per-note accuracy EMA and stray-note count.
    double accuracy_ema() const
    {
        return accuracy_ema_;
    }
    int wrong_count() const
    {
        return wrong_count_;
    }

    // Per-note learning outcomes (Roll mode, plans/scoreview-stream.md S0):
    // everything the player model aggregates. Hits carry the signed timing
    // delta in beats (negative = early) and a center-weighted quality;
    // misses and strays carry zeros. Auto-satisfied notes (ties) emit
    // nothing — they are not learning signals.
    struct NoteOutcome
    {
        std::string id; // element id; empty for stray notes
        double onset_q = 0.0; // source qstamp (strays: transport position)
        int pitch = -1;
        NoteVerdict verdict = NoteVerdict::Missed;
        bool stray = false; // matched no onset (wrong pitch / bad timing)
        double delta_q = 0.0; // hit: position - onset, in beats
        double quality = 0.0; // hit: 1 at center, kRollEdgeQuality at edge
    };
    // Chord-level outcome, emitted when a multi-note onset's window closes.
    struct ChordOutcome
    {
        double onset_q = 0.0;
        std::vector<int> pitches; // required pitches, sorted
        enum class Result : uint8_t
        {
            Clean, // all correct, struck together
            Split, // all correct, but spread beyond kChordSplitQ
            Miss, // at least one note missed
        } result = Result::Miss;
    };
    std::vector<NoteOutcome> take_note_outcomes();
    std::vector<ChordOutcome> take_chord_outcomes();

    VerdictUpdate take_verdict_update();

    int score() const
    {
        return score_;
    }
    int streak() const
    {
        return streak_;
    }
    int miss_count() const
    {
        return miss_count_;
    }

    static constexpr double kFallbackMarkingQpm = 120.0;
    static constexpr double kMinTempoFrac = 0.25;
    static constexpr double kMaxTempoFrac = 1.2;
    static constexpr double kStartTempoFrac = 0.6;
    // Pace adaptation: EMA over the player's gate-to-gate pace, eased into
    // the transport tempo at each gate opening; stalling at a gate beyond the
    // grace window decays tempo gently toward the floor.
    static constexpr double kPaceEmaAlpha = 0.35;
    static constexpr double kTempoEasePerGate = 0.25;
    static constexpr double kStallGraceSeconds = 4.0;
    static constexpr double kStallDecayPerSecond = 0.02;
    static constexpr int kBaseGatePoints = 10;
    static constexpr int kStreakBonusCap = 20; // +10% per streak step, capped
    // Roll mode (plans/scoreview-runner.md): hit windows in beats around
    // each onset's crossing, a per-note accuracy EMA, and per-onset tempo
    // easing — up slowly while accurate, down faster while struggling.
    static constexpr double kRollEarlyWindowQ = 0.45;
    static constexpr double kRollLateWindowQ = 0.45;
    static constexpr double kRollAccuracyAlpha = 0.12;
    static constexpr double kRollStartAccuracy = 0.7; // neutral: holds tempo
    static constexpr double kRollSpeedUpThreshold = 0.85;
    static constexpr double kRollSlowDownThreshold = 0.55;
    static constexpr double kRollTempoUpPerOnset = 0.010;
    static constexpr double kRollTempoDownPerOnset = 0.030;
    // Timing-weighted hits: quality tapers from 1 at the window center to
    // this floor at the edge — a sloppy hit still turns green but drags
    // the accuracy EMA (and so the tempo) down.
    static constexpr double kRollEdgeQuality = 0.25;
    // Chord notes struck further apart than this (beats) count as Split.
    static constexpr double kChordSplitQ = 0.2;

private:
    std::vector<Onset> onsets_;
    double duration_q_ = 0.0;
    double marking_qpm_ = kFallbackMarkingQpm;
    double canvas_width_ = 0.0;
    int join_miss_count_ = 0;
    int non_monotonic_count_ = 0;

    bool playing_ = false;
    double position_q_ = 0.0;
    double tempo_qpm_ = kFallbackMarkingQpm * kStartTempoFrac;
    size_t lit_cursor_ = 0;
    bool lit_reset_pending_ = false;

    // Gate state -----------------------------------------------------------
    void reset_gates();
    void open_gate(size_t index, bool clean, double t_seconds);
    void advance_armed();
    double next_required_qstamp() const;
    // Roll internals --------------------------------------------------------
    void judge_roll(const std::vector<PlayerNoteEvent>& events);
    void resolve_roll_passed();
    void accuracy_sample(double value);
    void adjust_roll_tempo();

    TransportMode mode_ = TransportMode::Clock;
    std::vector<Gate> gates_; // parallel to onsets_
    size_t armed_ = 0;
    std::vector<std::pair<std::string, NoteVerdict>> verdict_changes_;
    bool verdict_reset_pending_ = false;
    double stall_seconds_ = 0.0;
    double pace_ema_qpm_ = 0.0;
    double last_open_t_ = -1.0;
    double last_open_q_ = -1.0;
    int score_ = 0;
    int streak_ = 0;
    int miss_count_ = 0;

    // Roll state ------------------------------------------------------------
    size_t roll_resolved_ = 0; // onsets whose windows have closed
    double accuracy_ema_ = kRollStartAccuracy;
    int wrong_count_ = 0;
    std::vector<NoteOutcome> note_outcomes_;
    std::vector<ChordOutcome> chord_outcomes_;
};

} // namespace scoreview
} // namespace draxul
