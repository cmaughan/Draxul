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
    // Promotion: a bar counts mastered after this many CONSECUTIVE clean
    // complete passes — the strongest retention predictor is the share of
    // complete-correct traversals, and a mean lets a chronically fumbled
    // note ride along (30% wrong forever still averages 0.7). When EVERY
    // encountered bar promotes, the arc schedules the full performance run
    // and only then finishes.
    static constexpr int kPromotionCleanPasses = 3;
    // Day-scale spacing (C4): a mastered bar is DUE again after an expanding
    // gap — the optimal interval grows with the retention horizon, and
    // minute-scale gaps teach nothing. Indexed by the bar's day-separated
    // clean streak, capped at the last entry.
    static constexpr int kSpacedIntervalsDays[5] = { 1, 3, 7, 14, 30 };
    // The session opening serves at most this many time-due bars before the
    // arc resumes — reconsolidation first, never a wall of reviews.
    static constexpr int kMaxOpeningReviews = 6;
    // The FALLBACK revisit length. Arcs slice on detected phrases (C1); this
    // fixed window is used only when the piece has no confident structure —
    // fixed-length chunking is what the evidence argues against, so it must
    // never be the first choice.
    static constexpr int kSliceBars = 8;
    // Below this, the detected phrasing is mostly the hypermetric prior
    // guessing, so keep fixed slices rather than trust invented structure.
    // (Also the honest call for atonal material, where the research says
    // chunks are hand-shapes rather than tonal groupings.)
    static constexpr double kMinStructureConfidence = 0.35;
    // Serial position: recall collapses from a phrase's opening bar to its
    // tail, so at comparable mastery the later bar earns the review. Weighted
    // to break ties, never to override a genuinely weaker bar.
    static constexpr double kTailWeight = 0.15;
    // Phrase joins: practised as a pair below this mean mastery, capped so a
    // stubborn join cannot monopolise the specials.
    static constexpr double kSeamMasteryThreshold = 0.6;
    static constexpr int kMaxSeamsPerJoin = 2;

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
    // Urgent rewrite: splice a fix for the newest un-served fumble in at
    // `at_slot`, shifting the slot-indexed cooldowns to match. Shares the
    // once-per-fumble bookkeeping with try_reserve, so whichever path runs
    // first claims the fix and the other stays silent.
    int plan_urgent(StreamProgram& program, int at_slot) override;
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
    bool try_seam(StreamProgram& program, int slot);
    // Error re-serve: a bar whose LAST pass was fumbled returns at the next
    // planned slot — errors are corrected, never played past. Bounded by the
    // append-only program: with the engrave lookahead, "next planned" plays
    // roughly a window later (true mid-window injection is the rewriting
    // composer recorded on kanban 20).
    bool try_reserve(StreamProgram& program, int slot);
    // The newest fumbled bar that has not yet earned its fix (-1 = none);
    // shared by the append (try_reserve) and splice (plan_urgent) paths.
    int find_fix_bar() const;
    void begin_next_arc();
    // Aims the next arc at the weakest detected phrase; false when the piece
    // has no confident structure and the caller must fall back.
    bool begin_weakest_phrase(int total);
    void begin_weakest_slice(int total);

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
    std::map<int, int> last_seam_slot_; // phrase-tail bar -> slot
    std::map<int, int> seams_used_; // phrase-tail bar -> count
    std::map<int, int> reserved_at_pass_; // bar -> pass_count when re-served
    // C4 session opening: bars due by the day-scale schedule, dequeued into
    // the program's first slots. Built once per reset() from the model's
    // session day — the composer reads time through the model, never a clock.
    struct OpeningReview
    {
        int bar = -1;
        bool overnight_retest = false; // fumbled on an earlier day
        int gap_days = 0;
    };
    std::vector<OpeningReview> opening_queue_;
    // Convergence arc (S4): after the frontier finishes the piece, the
    // stream loops through the weakest phrase (C1; weakest fixed slice only
    // as a fallback) until every encountered bar promotes, then schedules the
    // full performance run.
    int arc_ = 0;
    int arc_start_bar_ = 0; // inclusive start of the current arc's bar range
    int arc_end_bar_ = -1; // exclusive end of the current arc's bar range
    bool arc_on_phrase_ = false; // arc aimed at a detected phrase, not a slice
    bool performance_run_ = false;
};

} // namespace scoreview
} // namespace draxul
