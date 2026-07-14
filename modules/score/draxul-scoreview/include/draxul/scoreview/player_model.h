#pragma once

// The player's memory (plans/scoreview-stream.md S0): pure aggregation of
// Roll-mode outcomes into the statistics the practice generator feeds on —
// per-pitch and per-onset hit/miss/timing, per-chord trouble, per-bar
// mastery with recent-encounter history (mastery is consistency over
// recent encounters, never a lifetime average), and session records.
// Serializes to versioned JSON (unknown fields preserved) for the
// per-piece progress file.

#include <draxul/scoreview/flow_controller.h>

#include <cstdint>
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
    };
    struct BarTally
    {
        int hit = 0;
        int miss = 0;
        HandTally left; // pitches below kHandSplitMidi
        HandTally right; // pitches at or above kHandSplitMidi
    };

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

    void apply(const FlowController::NoteOutcome& outcome);
    void apply(const FlowController::ChordOutcome& outcome);

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

    std::string extra_json_; // unknown top-level fields, preserved verbatim
};

} // namespace scoreview
} // namespace draxul
