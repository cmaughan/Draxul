#include <draxul/scoreview/player_model.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>

namespace draxul
{
namespace scoreview
{

using nlohmann::json;

namespace
{

constexpr int kSchemaVersion = 1;
// Keep the file bounded: forever-stats aggregate, session history caps.
constexpr size_t kMaxSessions = 200;

// Onset qstamps are map keys in JSON; fixed precision keeps them stable
// across float formatting differences.
std::string qstamp_key(double q)
{
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.4f", q);
    return buffer;
}

} // namespace

void PlayerModel::TimingStats::add(double delta_q)
{
    ++samples;
    const double d = delta_q - mean_q;
    mean_q += d / samples;
    m2_q += d * (delta_q - mean_q);
}

double PlayerModel::TimingStats::variance_q() const
{
    return samples > 1 ? m2_q / (samples - 1) : 0.0;
}

void PlayerModel::OnsetStats::push_recent(double quality)
{
    recent.push_back(quality);
    if (recent.size() > static_cast<size_t>(kRecentEncounters))
        recent.erase(recent.begin());
}

double PlayerModel::OnsetStats::recent_mean() const
{
    if (recent.empty())
        return 0.0;
    double sum = 0.0;
    for (const double q : recent)
        sum += q;
    return sum / static_cast<double>(recent.size());
}

void PlayerModel::set_piece(const std::string& title, double marking_qpm, double quarters_per_bar)
{
    title_ = title;
    marking_qpm_ = marking_qpm;
    quarters_per_bar_ = quarters_per_bar > 0.0 ? quarters_per_bar : 4.0;
}

void PlayerModel::begin_session(const std::string& start_iso)
{
    if (session_active_)
        return;
    Session session;
    session.start_iso = start_iso;
    sessions_.push_back(std::move(session));
    if (sessions_.size() > kMaxSessions)
        sessions_.erase(sessions_.begin());
    session_active_ = true;
    session_notes_ = 0;
}

void PlayerModel::end_session(int seconds, double tempo_frac)
{
    if (!session_active_ || sessions_.empty())
        return;
    Session& session = sessions_.back();
    session.seconds = seconds;
    session.notes = session_notes_;
    session.end_tempo_frac = tempo_frac;
    last_tempo_frac_ = tempo_frac;
    best_tempo_frac_ = std::max(best_tempo_frac_, tempo_frac);
    session_active_ = false;
}

void PlayerModel::apply(const FlowController::NoteOutcome& outcome)
{
    ++total_notes_;
    ++session_notes_;
    if (outcome.stray)
    {
        // Attribute the stray to nearby pitches: a fluff one or two
        // semitones off a known note is that note's trouble, not noise.
        for (auto& [pitch, stats] : pitch_)
        {
            if (std::abs(pitch - outcome.pitch) <= 2)
                ++stats.wrong_near;
        }
        return;
    }
    PitchStats& pitch = pitch_[outcome.pitch];
    // Drill-bar outcomes (onset_q sentinel) train pitch/chord statistics
    // but must not write mastery for a bar location they don't have.
    const bool drill = outcome.onset_q <= kDrillOnsetSentinel + 1.0;
    OnsetStats* onset = drill ? nullptr : &onset_[outcome.onset_q];
    if (outcome.verdict == FlowController::NoteVerdict::Correct)
    {
        ++pitch.hit;
        pitch.timing.add(outcome.delta_q);
        if (onset != nullptr)
        {
            ++onset->hit;
            onset->timing.add(outcome.delta_q);
            onset->push_recent(outcome.quality);
        }
    }
    else
    {
        ++pitch.miss;
        if (onset != nullptr)
        {
            ++onset->miss;
            onset->push_recent(0.0);
        }
    }
}

int PlayerModel::bar_encounters(int bar_index) const
{
    const double bar_start = bar_index * quarters_per_bar_;
    const double bar_end = bar_start + quarters_per_bar_;
    int count = 0;
    for (auto it = onset_.lower_bound(bar_start); it != onset_.end() && it->first < bar_end; ++it)
    {
        if (!it->second.recent.empty())
            ++count;
    }
    return count;
}

void PlayerModel::apply(const FlowController::ChordOutcome& outcome)
{
    ChordStats& stats = chord_[chord_key(outcome.pitches)];
    switch (outcome.result)
    {
    case FlowController::ChordOutcome::Result::Clean:
        ++stats.clean;
        break;
    case FlowController::ChordOutcome::Result::Split:
        ++stats.split;
        break;
    case FlowController::ChordOutcome::Result::Miss:
        ++stats.miss;
        break;
    }
}

double PlayerModel::bar_mastery(int bar_index) const
{
    const double bar_start = bar_index * quarters_per_bar_;
    const double bar_end = bar_start + quarters_per_bar_;
    double sum = 0.0;
    int count = 0;
    for (auto it = onset_.lower_bound(bar_start); it != onset_.end() && it->first < bar_end; ++it)
    {
        sum += it->second.recent_mean();
        ++count;
    }
    return count > 0 ? sum / count : 0.0;
}

std::string PlayerModel::chord_key(const std::vector<int>& sorted_pitches)
{
    std::string key;
    for (size_t i = 0; i < sorted_pitches.size(); ++i)
    {
        if (i > 0)
            key += '+';
        key += std::to_string(sorted_pitches[i]);
    }
    return key;
}

std::string PlayerModel::serialize() const
{
    json doc = extra_json_.empty() ? json::object() : json::parse(extra_json_, nullptr, false);
    if (doc.is_discarded() || !doc.is_object())
        doc = json::object();

    doc["version"] = kSchemaVersion;
    doc["piece"] = { { "title", title_ }, { "marking_qpm", marking_qpm_ },
        { "quarters_per_bar", quarters_per_bar_ } };
    doc["tempo"] = { { "best_frac", best_tempo_frac_ }, { "last_frac", last_tempo_frac_ } };
    doc["total_notes"] = total_notes_;

    json sessions = json::array();
    for (const Session& session : sessions_)
    {
        sessions.push_back({ { "start", session.start_iso }, { "seconds", session.seconds },
            { "notes", session.notes }, { "end_tempo_frac", session.end_tempo_frac } });
    }
    doc["sessions"] = sessions;

    json pitch = json::object();
    for (const auto& [midi, stats] : pitch_)
    {
        pitch[std::to_string(midi)] = { { "hit", stats.hit }, { "miss", stats.miss },
            { "wrong_near", stats.wrong_near }, { "dt_mean_q", stats.timing.mean_q },
            { "dt_var_q", stats.timing.variance_q() }, { "dt_n", stats.timing.samples },
            { "dt_m2", stats.timing.m2_q } };
    }
    doc["pitch"] = pitch;

    json onset = json::object();
    for (const auto& [q, stats] : onset_)
    {
        onset[qstamp_key(q)] = { { "hit", stats.hit }, { "miss", stats.miss },
            { "dt_mean_q", stats.timing.mean_q }, { "dt_n", stats.timing.samples },
            { "dt_m2", stats.timing.m2_q }, { "recent", stats.recent } };
    }
    doc["onset"] = onset;

    json chord = json::object();
    for (const auto& [key, stats] : chord_)
    {
        chord[key] = { { "clean", stats.clean }, { "split", stats.split },
            { "miss", stats.miss } };
    }
    doc["chord"] = chord;

    return doc.dump(2);
}

bool PlayerModel::deserialize(const std::string& json_text)
{
    const json doc = json::parse(json_text, nullptr, false);
    if (doc.is_discarded() || !doc.is_object())
        return false;

    pitch_.clear();
    onset_.clear();
    chord_.clear();
    sessions_.clear();

    if (const auto piece = doc.find("piece"); piece != doc.end() && piece->is_object())
    {
        title_ = piece->value("title", title_);
        marking_qpm_ = piece->value("marking_qpm", marking_qpm_);
        quarters_per_bar_ = piece->value("quarters_per_bar", quarters_per_bar_);
    }
    if (const auto tempo = doc.find("tempo"); tempo != doc.end() && tempo->is_object())
    {
        best_tempo_frac_ = tempo->value("best_frac", 0.0);
        last_tempo_frac_ = tempo->value("last_frac", 0.0);
    }
    total_notes_ = doc.value("total_notes", 0);

    if (const auto sessions = doc.find("sessions"); sessions != doc.end() && sessions->is_array())
    {
        for (const json& entry : *sessions)
        {
            Session session;
            session.start_iso = entry.value("start", "");
            session.seconds = entry.value("seconds", 0);
            session.notes = entry.value("notes", 0);
            session.end_tempo_frac = entry.value("end_tempo_frac", 0.0);
            sessions_.push_back(std::move(session));
        }
    }
    if (const auto pitch = doc.find("pitch"); pitch != doc.end() && pitch->is_object())
    {
        for (const auto& [key, value] : pitch->items())
        {
            PitchStats stats;
            stats.hit = value.value("hit", 0);
            stats.miss = value.value("miss", 0);
            stats.wrong_near = value.value("wrong_near", 0);
            stats.timing.samples = value.value("dt_n", 0);
            stats.timing.mean_q = value.value("dt_mean_q", 0.0);
            stats.timing.m2_q = value.value("dt_m2", 0.0);
            pitch_[std::stoi(key)] = std::move(stats);
        }
    }
    if (const auto onset = doc.find("onset"); onset != doc.end() && onset->is_object())
    {
        for (const auto& [key, value] : onset->items())
        {
            OnsetStats stats;
            stats.hit = value.value("hit", 0);
            stats.miss = value.value("miss", 0);
            stats.timing.samples = value.value("dt_n", 0);
            stats.timing.mean_q = value.value("dt_mean_q", 0.0);
            stats.timing.m2_q = value.value("dt_m2", 0.0);
            if (const auto recent = value.find("recent");
                recent != value.end() && recent->is_array())
            {
                for (const json& q : *recent)
                    stats.recent.push_back(q.get<double>());
            }
            onset_[std::stod(key)] = std::move(stats);
        }
    }
    if (const auto chord = doc.find("chord"); chord != doc.end() && chord->is_object())
    {
        for (const auto& [key, value] : chord->items())
        {
            ChordStats stats;
            stats.clean = value.value("clean", 0);
            stats.split = value.value("split", 0);
            stats.miss = value.value("miss", 0);
            chord_[key] = stats;
        }
    }

    // Preserve fields this build doesn't understand (newer schema data).
    json extra = doc;
    for (const char* known : { "version", "piece", "tempo", "total_notes", "sessions",
             "pitch", "onset", "chord" })
        extra.erase(known);
    extra_json_ = extra.empty() ? std::string() : extra.dump();

    session_active_ = false;
    session_notes_ = 0;
    return true;
}

} // namespace scoreview
} // namespace draxul
