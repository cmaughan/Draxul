#pragma once

// The composer (plans/scoreview-stream.md S3): decides what the endless
// stream feeds the player, one bar slot at a time, from the live player
// model — piece bars at a walking frontier, spaced REVIEW slices of weak
// bars, and fabricated DRILL bars built from the exact voiced chords the
// player fumbles. Pure and deterministic given its inputs; the host maps
// slots to windows and outcomes back through provenance.

#include <draxul/scoreview/piece_analysis.h>
#include <draxul/scoreview/player_model.h>
#include <draxul/scoreview/source_slicer.h>

#include <map>
#include <string>
#include <vector>

namespace draxul
{
namespace scoreview
{

struct StreamBarPlan
{
    enum class Kind : uint8_t
    {
        Piece, // the frontier bar, verbatim
        Review, // a weak earlier bar, revisited (spaced repetition)
        Drill, // a fabricated exercise bar
    };
    Kind kind = Kind::Piece;
    // Piece/Review: the bar to play. Drill: the reference bar whose
    // attribute state and provenance context the drill inherits.
    int source_bar = -1;
    std::string drill_xml; // Drill only: the fabricated <measure>
    std::string reason; // human-readable, for logs and debugging
};

class StreamComposer
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

    void configure(
        const SourceSlicer* slicer, const PlayerModel* model, const PieceProfile* profile);
    bool ready() const
    {
        return slicer_ != nullptr && model_ != nullptr;
    }
    void reset();

    // Extends the program up to `slots` entries (stops early when the
    // frontier finishes); returns the planned count.
    int ensure(int slots);
    int planned() const
    {
        return static_cast<int>(program_.size());
    }
    bool finished() const
    {
        return finished_;
    }
    const StreamBarPlan& plan(int slot) const
    {
        return program_[static_cast<size_t>(slot)];
    }

    // Stream-axis geometry over the program (slot 0 starts at 0).
    double slot_start_q(int slot) const;
    double slot_quarters(int slot) const;
    int slot_at(double stream_q) const;

    // The fabricated chord-drill measure for a "60+64+67"-style key, in the
    // attribute context of `reference_bar` (public for tests). Broken form
    // arpeggiates the grab (the easier rung); block form strikes it whole.
    std::string fabricate_chord_drill(
        const std::string& chord_key, int reference_bar, bool broken) const;
    // A one-bar scale fragment in the piece's key, running through the
    // troubled register (public for tests).
    std::string fabricate_scale_bar(int reference_bar, int center_pitch) const;

private:
    void compose_next();
    bool try_hands(int slot);
    bool try_drill(int slot);
    bool try_scale(int slot);
    bool try_review(int slot);
    void begin_next_arc();

    const SourceSlicer* slicer_ = nullptr;
    const PlayerModel* model_ = nullptr;
    const PieceProfile* profile_ = nullptr;

    std::vector<StreamBarPlan> program_;
    std::vector<double> slot_start_q_; // size = program_.size() + 1
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
