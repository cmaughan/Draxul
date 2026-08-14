#pragma once

// The player's memory (plans/scoreview-stream.md S0): pure aggregation of
// Roll-mode outcomes into the statistics the practice generator feeds on —
// per-pitch and per-onset hit/miss/timing, per-chord trouble, per-bar
// mastery with recent-encounter history (mastery is consistency over
// recent encounters, never a lifetime average), and session records.
// Serializes to versioned JSON (unknown fields preserved) for the
// per-piece progress file.

#include <draxul/scoreview/note_outcomes.h>

#include <map>
#include <string>
#include <vector>

namespace draxul
{
namespace scoreview
{

class PlayerModel
{
public:
    // Recent-encounter ring length: mastery asks "were the LAST few clean
    // and in time", which a lucky pass or an ancient average can't fake.
    static constexpr int kRecentEncounters = 8;

    struct TimingStats
    {
        int samples = 0;
        double mean_q = 0.0;
        double m2_q = 0.0; // Welford accumulator

        void add(double delta_q);
        double variance_q() const;
    };

    struct PitchStats
    {
        int hit = 0;
        int miss = 0;
        int wrong_near = 0; // strays within a whole tone of this pitch
        TimingStats timing;
    };

    struct OnsetStats
    {
        int hit = 0;
        int miss = 0;
        TimingStats timing;
        // Quality of the last kRecentEncounters encounters (0 = miss).
        std::vector<double> recent;

        void push_recent(double quality);
        double recent_mean() const;
    };

    struct ChordStats
    {
        int clean = 0;
        int split = 0;
        int miss = 0;
    };

    // Per-bar right/wrong, split into the two hands. The hand boundary is a
    // heuristic — notes below middle C count as the left hand, at or above as
    // the right — since the true split is fuzzy (as the player noted).
    static constexpr int kHandSplitMidi = 60;
    struct HandTally
    {
        int hit = 0;
        int miss = 0;
        std::vector<uint8_t> recent_passes; // capped at kRecentEncounters
        int consecutive_clean = 0;
        int pass_count = 0;
    };
    struct BarTally
    {
        int hit = 0;
        int miss = 0;
        HandTally left; // pitches below kHandSplitMidi
        HandTally right; // pitches at or above kHandSplitMidi
        // Complete passes of this bar (one traversal each), newest last —
        // 1 = clean (every note Correct, no strays), 0 = fumbled. The
        // strongest retention predictor in the evidence base is the share
        // of COMPLETE CORRECT passes, so promotion reads this, never the
        // mean of note qualities (a chronic 30%-wrong bar has a fine mean).
        std::vector<uint8_t> recent_passes; // capped at kRecentEncounters
        int consecutive_clean = 0; // trailing clean passes
        int pass_count = 0; // lifetime passes
        // Day-scale spacing (C4): the civil day of the bar's newest pass,
        // and how many successive DAY-SEPARATED clean re-encounters it has
        // survived — the expanding-interval schedule reads this, because
        // minute-scale gaps demonstrably teach nothing (no forgetting
        // happens, so nothing is reconstructed).
        int last_pass_day = 0; // 0 = never traversed
        int spaced_streak = 0; // day-separated clean re-encounters
        // The bar's tempo ladder, as a fraction of the piece's marking:
        // clean passes raise it, fumbles lower it. Speed is an OUTPUT of
        // accurate passes (submaximal practice raises ceiling speed), so
        // the roll tempo is CAPPED at this rung while inside the bar.
        double ladder_frac = 0.0; // 0 = never played (kLadderStart applies)
    };
    // Ladder tuning: start submaximal, climb gently on clean passes, drop
    // harder on fumbles (errors are expensive), never below the floor or
    // above the marking.
    static constexpr double kLadderStart = 0.60;
    static constexpr double kLadderStep = 0.04;
    static constexpr double kLadderDrop = 0.08;
    static constexpr double kLadderFloor = 0.10;
    static constexpr double kLadderCap = 1.0;

    struct Session
    {
        std::string start_iso;
        int seconds = 0;
        int notes = 0;
        double end_tempo_frac = 0.0;
    };

    // Piece identity + geometry (bar mastery needs the bar length).
    void set_piece(const std::string& title, double marking_qpm, double quarters_per_bar);

    void begin_session(const std::string& start_iso);
    void end_session(int seconds, double tempo_frac);
    bool session_active() const
    {
        return session_active_;
    }

    void apply(const NoteOutcome& outcome);
    void apply(const ChordOutcome& outcome);

    // Aggregate views ------------------------------------------------------
    const std::map<int, PitchStats>& pitch_stats() const
    {
        return pitch_;
    }
    const std::map<double, OnsetStats>& onset_stats() const
    {
        return onset_;
    }
    const std::map<std::string, ChordStats>& chord_stats() const
    {
        return chord_;
    }
    const std::vector<Session>& sessions() const
    {
        return sessions_;
    }
    // Per-bar (source-bar keyed) right/wrong tallies with the two hands.
    const std::map<int, BarTally>& bar_tally() const
    {
        return bar_tally_;
    }
    // Wipe the learning record for this piece (keeps its identity — title,
    // marking, meter — so the same piece just starts fresh).
    void clear_progress();
    // Mean recent-encounter quality of the bar's onsets, 0..1 (0 when the
    // bar has never been encountered).
    double bar_mastery(int bar_index) const;
    // Number of the bar's onsets with at least one encounter (0 mastery is
    // ambiguous between "all missed" and "never played"; this disambiguates).
    int bar_encounters(int bar_index) const;
    // Complete-pass views (see BarTally::recent_passes). A pass closes when
    // the outcome stream moves to a different bar — each stream slot is a
    // whole bar, so interleaved reviews and drills close passes correctly —
    // or when the session ends.
    int bar_consecutive_clean(int bar_index) const;
    int bar_pass_count(int bar_index) const;
    int bar_hand_consecutive_clean(int bar_index, int staff) const;
    int bar_hand_pass_count(int bar_index, int staff) const;
    // True when the bar has at least one pass and the newest was fumbled —
    // the composer's re-serve trigger.
    bool bar_last_pass_dirty(int bar_index) const;
    bool bar_hand_last_pass_dirty(int bar_index, int staff) const;
    // Monotonic count of fumbled passes — the host's cheap "a fumble just
    // happened" edge detector for the urgent program rewrite.
    int dirty_pass_count() const
    {
        return dirty_pass_count_;
    }
    // The bar's tempo-ladder rung as a fraction of the marking
    // (kLadderStart when the bar has never been traversed).
    double bar_tempo_ladder(int bar_index) const;
    // Day-scale views (C4). current_day() is the active session's civil day
    // (0 when no session or unparseable) — the composer reads time through
    // the model, never a clock, so planning stays pure and replayable.
    int current_day() const
    {
        return current_day_;
    }
    int bar_last_pass_day(int bar_index) const;
    int bar_spaced_streak(int bar_index) const;
    // Days since the civil epoch for an ISO-8601 timestamp ("YYYY-MM-DD..."),
    // 0 on parse failure. Public for tests and the composer's due math.
    static int civil_day_from_iso(const std::string& iso);
    // Closes the in-flight pass, recording it against its bar. Called on bar
    // transition internally and by the session end.
    void close_open_pass();
    // Trailing consecutive CLEAN encounters of one onset (0 when never
    // played) — the guidance keyboard's confidence measure.
    int onset_trailing_correct(double onset_q) const;
    // Outcomes from fabricated drill bars carry this onset_q sentinel: they
    // feed pitch and chord statistics but never bar/onset mastery.
    static constexpr double kDrillOnsetSentinel = -1e6;
    double best_tempo_frac() const
    {
        return best_tempo_frac_;
    }
    double last_tempo_frac() const
    {
        return last_tempo_frac_;
    }
    int total_notes_judged() const
    {
        return total_notes_;
    }

    // Versioned JSON round-trip. deserialize() returns false on parse
    // failure (caller keeps a fresh model); unknown JSON fields survive a
    // load-save cycle so newer builds' data is never destroyed.
    std::string serialize() const;
    bool deserialize(const std::string& json_text);

    static std::string chord_key(const std::vector<int>& sorted_pitches);

private:
    std::string title_;
    double marking_qpm_ = 0.0;
    double quarters_per_bar_ = 4.0;

    std::map<int, PitchStats> pitch_;
    std::map<double, OnsetStats> onset_;
    std::map<std::string, ChordStats> chord_;
    std::map<int, BarTally> bar_tally_;
    std::vector<Session> sessions_;
    double best_tempo_frac_ = 0.0;
    double last_tempo_frac_ = 0.0;
    int total_notes_ = 0;

    bool session_active_ = false;
    int session_notes_ = 0;

    int current_day_ = 0; // active session's civil day (0 = unknown)
    int dirty_pass_count_ = 0; // lifetime fumbled passes (monotonic)

    // The in-flight pass: which bar the outcome stream is currently inside,
    // and whether anything in it has gone wrong yet.
    int open_pass_bar_ = -1;
    bool open_pass_dirty_ = false;
    int open_pass_outcomes_ = 0;
    bool open_pass_left_seen_ = false;
    bool open_pass_left_dirty_ = false;
    bool open_pass_right_seen_ = false;
    bool open_pass_right_dirty_ = false;

    std::string extra_json_; // unknown top-level fields, preserved verbatim
};

} // namespace scoreview
} // namespace draxul
