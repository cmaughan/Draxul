#include <draxul/scoreview/score_timemap.h>

#include <unordered_map>

#include <nlohmann/json.hpp>

#include <algorithm>

namespace draxul
{
namespace scoreview
{

std::optional<Timemap> parse_timemap(std::string_view json_text, std::string& error)
{
    const nlohmann::json doc = nlohmann::json::parse(json_text, /*cb=*/nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded())
    {
        error = "timemap is not valid JSON";
        return std::nullopt;
    }
    if (!doc.is_array())
    {
        error = "timemap root is not an array";
        return std::nullopt;
    }

    Timemap timemap;
    timemap.entries.reserve(doc.size());
    for (const nlohmann::json& item : doc)
    {
        if (!item.is_object() || !item.contains("qstamp") || !item["qstamp"].is_number())
            continue;
        TimemapEntry entry;
        entry.qstamp = item["qstamp"].get<double>();

        const auto read_ids = [&item](const char* key, std::vector<std::string>& out) {
            const auto it = item.find(key);
            if (it == item.end() || !it->is_array())
                return;
            for (const nlohmann::json& id : *it)
            {
                if (id.is_string())
                    out.push_back(id.get<std::string>());
            }
        };
        read_ids("on", entry.note_on);
        read_ids("off", entry.note_off);

        if (timemap.tempo_qpm == 0.0)
        {
            const auto tempo = item.find("tempo");
            if (tempo != item.end() && tempo->is_number())
                timemap.tempo_qpm = tempo->get<double>();
        }
        timemap.entries.push_back(std::move(entry));
    }

    // Verovio emits playback order; sort defensively so downstream code can
    // rely on the ascending-qstamp invariant.
    std::stable_sort(timemap.entries.begin(), timemap.entries.end(),
        [](const TimemapEntry& a, const TimemapEntry& b) { return a.qstamp < b.qstamp; });
    if (!timemap.entries.empty())
        timemap.duration_q = timemap.entries.back().qstamp;
    return timemap;
}

std::vector<AnalysisOnset> analysis_onsets_from_timemap(const Timemap& timemap,
    const std::function<int(const std::string&)>& midi_pitch,
    std::vector<std::vector<std::string>>* ids_out)
{
    // First pass: when each id stops sounding (ids are unique per note).
    std::unordered_map<std::string, double> off_q;
    off_q.reserve(timemap.entries.size() * 2);
    for (const TimemapEntry& entry : timemap.entries)
    {
        for (const std::string& id : entry.note_off)
            off_q[id] = entry.qstamp;
    }
    std::vector<AnalysisOnset> onsets;
    onsets.reserve(timemap.entries.size());
    if (ids_out != nullptr)
        ids_out->reserve(timemap.entries.size());
    for (const TimemapEntry& entry : timemap.entries)
    {
        AnalysisOnset onset;
        onset.qstamp = entry.qstamp;
        std::vector<std::string> ids;
        for (const std::string& id : entry.note_on)
        {
            const int pitch = midi_pitch ? midi_pitch(id) : -1;
            if (pitch < 0)
                continue;
            onset.pitches.push_back(pitch);
            const auto found = off_q.find(id);
            onset.durations.push_back(
                found != off_q.end() ? std::max(0.0, found->second - entry.qstamp) : 0.0);
            ids.push_back(id);
        }
        if (onset.pitches.empty())
            continue;
        onsets.push_back(std::move(onset));
        if (ids_out != nullptr)
            ids_out->push_back(std::move(ids));
    }
    return onsets;
}

} // namespace scoreview
} // namespace draxul
