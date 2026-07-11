#pragma once

#include <draxul/scoreview/score_draw_list.h>
#include <draxul/scoreview/score_timemap.h>

#include <string>
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

    static constexpr double kFallbackMarkingQpm = 120.0;
    static constexpr double kMinTempoFrac = 0.25;
    static constexpr double kMaxTempoFrac = 1.2;
    static constexpr double kStartTempoFrac = 0.6;

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
};

} // namespace scoreview
} // namespace draxul
