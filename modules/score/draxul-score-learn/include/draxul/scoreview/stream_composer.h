#pragma once

// The composer (plans/scoreview-stream.md S3): decides what the endless
// stream feeds the player, one bar slot at a time, from the live player
// model — piece bars at a walking frontier, spaced REVIEW slices of weak
// bars, and fabricated DRILL bars built from the exact voiced chords the
// player fumbles. Pure and deterministic given its inputs; it extends a
// host-owned StreamProgram, and the host maps slots to windows and outcomes
// back through the program's provenance.

#include <draxul/scoreview/composer.h>

#include <map>
#include <string>

namespace draxul
{
namespace scoreview
{

class StreamComposer final : public IComposer
{
public:
    // Mastery below this schedules a review; encounters required first.
    static constexpr double kReviewMasteryThreshold = 0.5;
    static constexpr int kMaxReviewsPerBar = 3;
    // Chord trouble (miss + split - clean) at or above this earns a drill.
    static constexpr int kDrillTroubleThreshold = 3;
    // Never boring: at least this many piece bars between specials, and a
    // per-chord cooldown between repeats of the same drill.
    static constexpr int kMinPieceBarsBetweenSpecials = 2;
    static constexpr int kDrillCooldownSlots = 10;
    // The simplification ladder (S4): below this mastery a struggling bar
    // gets its WEAK HAND ALONE before the full bar returns.
    static constexpr double kHandsSeparateMastery = 0.3;
    // Register trouble (missed pitches inside an octave window) at or above
    // this earns a scale fragment in the piece's key.
    static constexpr int kScaleTroubleThreshold = 5;
    // Promotion: a bar counts mastered at this recent-encounter quality;
    // when EVERY encountered bar promotes, the arc schedules the full
    // performance run and only then finishes.
    static constexpr double kPromotionMastery = 0.7;
    static constexpr int kSliceBars = 8; // weakest-slice revisit length

    const char* name() const override
    {
        return "adaptive-stream";
    }
    // Fabricated drill bars are written for the grand staff (one part);
    // multi-part pieces stream the source verbatim instead.
    bool supports(const SourceSlicer& slicer) const override
    {
        return slicer.ready() && slicer.part_count() == 1;
    }
    void configure(const SourceSlicer* slicer, const PlayerModel* model,
        const PieceProfile* profile) override;
    bool ready() const override
    {
        return slicer_ != nullptr && model_ != nullptr;
    }
    // Clears composer policy state only; the host clears the paired program
    // in the same breath (ScoreHost::reset_stream_plan) — cooldowns are slot
    // indexed, so composer state and program must never diverge.
    void reset() override;

    // Extends `program` up to `slots` entries (stops early when the
    // frontier finishes); returns the planned count. Must always be handed
    // the same program this composer has been extending since reset().
    int ensure(StreamProgram& program, int slots) override;
    bool finished() const override
    {
        return finished_;
    }

    // The fabricated chord-drill measure for a "60+64+67"-style key, in the
    // attribute context of `reference_bar` (public for tests). Broken form
    // arpeggiates the grab (the easier rung); block form strikes it whole.
    std::string fabricate_chord_drill(
        const std::string& chord_key, int reference_bar, bool broken) const;
    // A one-bar scale fragment in the piece's key, running through the
    // troubled register (public for tests).
    std::string fabricate_scale_bar(int reference_bar, int center_pitch) const;

private:
    void compose_next(StreamProgram& program);
    bool try_hands(StreamProgram& program, int slot);
    bool try_drill(StreamProgram& program, int slot);
    bool try_scale(StreamProgram& program, int slot);
    bool try_review(StreamProgram& program, int slot);
    void begin_next_arc();

    const SourceSlicer* slicer_ = nullptr;
    const PlayerModel* model_ = nullptr;
    const PieceProfile* profile_ = nullptr;

    int frontier_ = 0;
    bool finished_ = false;
    int piece_bars_since_special_ = 0;
    int specials_count_ = 0; // rotates the special chain for variety
    std::map<std::string, int> last_drill_slot_; // chord key -> slot
    std::map<std::string, int> drill_stage_; // chord key -> rungs climbed
    std::map<int, int> reviews_used_; // source bar -> count
    std::map<int, int> last_review_slot_; // source bar -> slot
    std::map<int, bool> hands_done_; // source bar -> weak hand played alone
    std::map<int, int> last_scale_slot_; // register window -> slot
    // Convergence arc (S4): after the frontier finishes the piece, the
    // stream loops through the weakest slices until every encountered bar
    // promotes, then schedules the full performance run.
    int arc_ = 0;
    int arc_end_bar_ = -1; // exclusive end of the current arc's bar range
    bool performance_run_ = false;
};

} // namespace scoreview
} // namespace draxul
