#pragma once

// Player-memory session + persistence (kanban 21, ScoreSessionController):
// the per-piece player model, its content-hash-keyed progress file, the
// session clock, the bar-boundary flush policy, and the cached piece
// analysis with its inspection dump. Owns every filesystem/serialization
// detail; ScoreRuntime keeps only the transport-coupled decisions (tempo
// resume on session start, ending tempo fraction). Internal to
// draxul-scoreview-runtime (no public header).

#include <draxul/scoreview/piece_analysis.h>
#include <draxul/scoreview/player_model.h>

#include <chrono>
#include <filesystem>
#include <string>

namespace draxul
{
namespace scoreview
{

class ScoreSessionController
{
public:
    // Keys the progress file by the source bytes (renames keep history) and
    // loads any stored record into the model.
    void attach_source(
        const std::filesystem::path& progress_dir, const std::string& source_bytes);
    bool attached() const
    {
        return !progress_path_.empty();
    }

    PlayerModel& model()
    {
        return model_;
    }
    const PlayerModel& model() const
    {
        return model_;
    }

    // The upfront analysis (S1), cached for the composer and dumped beside
    // the progress file (<hash>.analysis.json) for inspection.
    const PieceProfile& piece_profile() const
    {
        return profile_;
    }
    void set_piece_profile(PieceProfile profile);

    // Session clock. begin_session stamps now and returns whether a fresh
    // session actually started (false when unattached or already active);
    // end_session records the duration + ending tempo fraction and
    // final-flushes. Both are no-ops in the off states.
    bool begin_session();
    void end_session(double end_tempo_frac);

    void mark_dirty()
    {
        dirty_ = true;
    }
    // The cheap incremental save: flushes when dirty and the bar changed.
    void flush_at_bar(int bar);
    void save(bool final_flush);
    // Wipes the learning record (keeps the piece identity); callers decide
    // when to re-begin the session and persist.
    void clear_progress();

private:
    PlayerModel model_;
    PieceProfile profile_;
    std::filesystem::path progress_path_;
    std::chrono::steady_clock::time_point session_start_{};
    int last_flush_bar_ = -1;
    bool dirty_ = false;
};

} // namespace scoreview
} // namespace draxul
