#include "score_session_controller.h"

#include <draxul/log.h>
#include <draxul/scoreview/progress_store.h>

#include <cmath>
#include <ctime>

namespace draxul
{
namespace scoreview
{

namespace
{

std::string now_iso8601()
{
    const std::time_t t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm_utc{};
#ifdef _WIN32
    gmtime_s(&tm_utc, &t);
#else
    gmtime_r(&t, &tm_utc);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
    return buffer;
}

} // namespace

void ScoreSessionController::attach_source(
    const std::filesystem::path& progress_dir, const std::string& source_bytes)
{
    progress_path_ = progress_path(progress_dir, source_bytes);
    const std::string stored = load_progress(progress_path_);
    if (!stored.empty() && model_.deserialize(stored))
    {
        DRAXUL_LOG_INFO(LogCategory::App,
            "score: progress loaded — %zu session(s), %d notes, best tempo %d%%",
            model_.sessions().size(), model_.total_notes_judged(),
            static_cast<int>(std::lround(model_.best_tempo_frac() * 100.0)));
    }
    else if (!stored.empty())
    {
        DRAXUL_LOG_WARN(LogCategory::App, "score: progress file unreadable, starting fresh (%s)",
            progress_path_.string().c_str());
    }
}

void ScoreSessionController::set_piece_profile(PieceProfile profile)
{
    profile_ = std::move(profile);
    DRAXUL_LOG_INFO(LogCategory::App,
        "score: analysis — key %s (%.2f), %zu chord(s), %zu motif(s), %zu figure(s)",
        key_name(profile_.global_key.tonic_pc, profile_.global_key.minor).c_str(),
        profile_.global_key.confidence, profile_.chords.size(), profile_.motifs.size(),
        profile_.figures.size());
    if (!progress_path_.empty())
    {
        std::filesystem::path analysis_path = progress_path_;
        analysis_path.replace_extension(".analysis.json");
        std::string save_error;
        if (!save_progress_atomic(analysis_path, profile_.serialize(), save_error))
            DRAXUL_LOG_WARN(
                LogCategory::App, "score: analysis dump failed: %s", save_error.c_str());
    }
}

bool ScoreSessionController::begin_session()
{
    if (progress_path_.empty() || model_.session_active())
        return false;
    model_.begin_session(now_iso8601());
    session_start_ = std::chrono::steady_clock::now();
    return true;
}

void ScoreSessionController::end_session(double end_tempo_frac)
{
    if (!model_.session_active())
        return;
    const int seconds = static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - session_start_)
            .count());
    model_.end_session(seconds, end_tempo_frac);
    save(/*final_flush=*/true);
}

void ScoreSessionController::flush_at_bar(int bar)
{
    if (bar == last_flush_bar_)
        return;
    last_flush_bar_ = bar;
    if (dirty_)
        save(/*final_flush=*/false);
}

void ScoreSessionController::save(bool final_flush)
{
    if (progress_path_.empty())
        return;
    std::string error;
    if (!save_progress_atomic(progress_path_, model_.serialize(), error))
    {
        DRAXUL_LOG_WARN(LogCategory::App, "score: progress save failed: %s", error.c_str());
        return;
    }
    dirty_ = false;
    if (final_flush)
    {
        DRAXUL_LOG_INFO(LogCategory::App, "score: progress saved — %d notes total, %s",
            model_.total_notes_judged(), progress_path_.string().c_str());
    }
}

void ScoreSessionController::clear_progress()
{
    model_.clear_progress();
}

} // namespace scoreview
} // namespace draxul
