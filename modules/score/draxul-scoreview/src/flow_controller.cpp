#include <draxul/scoreview/flow_controller.h>

#include <algorithm>
#include <limits>
#include <map>
#include <unordered_map>

namespace draxul
{
namespace scoreview
{

namespace
{

// Reference x of a draw op in strip canvas units (the notehead position for
// glyphs; good enough for playhead interpolation everywhere else).
float op_reference_x(const GlyphInstance& glyph)
{
    return glyph.xform.e;
}

float op_reference_x(const DrawPath& path)
{
    return path.cmds.empty() ? 0.0f : path.cmds.front().p.x;
}

float op_reference_x(const DrawText& text)
{
    return text.pos.x;
}

} // namespace

bool FlowController::build(const Timemap& timemap, const ScoreDrawList& strip, std::string& error)
{
    onsets_.clear();
    join_miss_count_ = 0;
    non_monotonic_count_ = 0;
    duration_q_ = timemap.duration_q;
    marking_qpm_ = timemap.tempo_qpm > 0.0 ? timemap.tempo_qpm : kFallbackMarkingQpm;
    canvas_width_ = strip.canvas_size.x;
    tempo_qpm_ = marking_qpm_ * kStartTempoFrac;
    position_q_ = 0.0;
    playing_ = false;
    lit_cursor_ = 0;
    lit_reset_pending_ = false;

    // Minimum reference x per element id across all op kinds.
    std::unordered_map<std::string, float> id_min_x;
    id_min_x.reserve(strip.glyphs.size() + strip.paths.size());
    const auto note_min = [&id_min_x](const std::string& id, float x) {
        if (id.empty())
            return;
        auto [it, inserted] = id_min_x.emplace(id, x);
        if (!inserted)
            it->second = std::min(it->second, x);
    };
    for (const GlyphInstance& glyph : strip.glyphs)
        note_min(glyph.element_id, op_reference_x(glyph));
    for (const DrawPath& path : strip.paths)
        note_min(path.element_id, op_reference_x(path));
    for (const DrawText& text : strip.texts)
        note_min(text.element_id, op_reference_x(text));

    // Merge same-qstamp entries and join ids to x positions.
    std::map<double, Onset> merged;
    for (const TimemapEntry& entry : timemap.entries)
    {
        if (entry.note_on.empty())
            continue;
        Onset& onset = merged[entry.qstamp];
        onset.qstamp = entry.qstamp;
        float x = std::numeric_limits<float>::max();
        for (const std::string& id : entry.note_on)
        {
            const auto found = id_min_x.find(id);
            if (found == id_min_x.end())
            {
                ++join_miss_count_;
                continue;
            }
            x = std::min(x, found->second);
            onset.ids.push_back(id);
        }
        if (onset.ids.empty())
        {
            merged.erase(entry.qstamp);
            continue;
        }
        onset.x = x;
    }

    onsets_.reserve(merged.size());
    float prev_x = 0.0f;
    for (auto& [q, onset] : merged)
    {
        // Repeats can revisit earlier measures (x jumps backward); clamp to
        // forward motion for this milestone (plans/scoreview-conveyor.md).
        if (!onsets_.empty() && onset.x < prev_x)
        {
            ++non_monotonic_count_;
            onset.x = prev_x;
        }
        prev_x = onset.x;
        onsets_.push_back(std::move(onset));
    }

    if (onsets_.empty())
    {
        error = "timemap joined zero onsets against the strip";
        return false;
    }
    return true;
}

void FlowController::play()
{
    if (ready() && !at_end())
        playing_ = true;
}

void FlowController::pause()
{
    playing_ = false;
}

void FlowController::rewind()
{
    seek(0.0);
    playing_ = false;
}

void FlowController::seek(double qstamp)
{
    const double clamped = std::clamp(qstamp, 0.0, duration_q_);
    if (clamped < position_q_)
    {
        lit_cursor_ = 0;
        lit_reset_pending_ = true;
    }
    position_q_ = clamped;
}

void FlowController::advance(double wall_dt_seconds)
{
    if (!playing_ || wall_dt_seconds <= 0.0)
        return;
    position_q_ += wall_dt_seconds * tempo_qpm_ / 60.0;
    if (position_q_ >= duration_q_)
    {
        position_q_ = duration_q_;
        playing_ = false;
    }
}

bool FlowController::at_end() const
{
    return duration_q_ > 0.0 && position_q_ >= duration_q_;
}

void FlowController::set_tempo_qpm(double qpm)
{
    tempo_qpm_ = std::clamp(qpm, min_tempo_qpm(), max_tempo_qpm());
}

double FlowController::min_tempo_qpm() const
{
    return marking_qpm_ * kMinTempoFrac;
}

double FlowController::max_tempo_qpm() const
{
    return marking_qpm_ * kMaxTempoFrac;
}

double FlowController::x_at(double qstamp) const
{
    if (onsets_.empty())
        return 0.0;
    if (qstamp <= onsets_.front().qstamp)
        return onsets_.front().x;
    if (qstamp >= onsets_.back().qstamp)
        return onsets_.back().x;
    const auto after = std::upper_bound(onsets_.begin(), onsets_.end(), qstamp,
        [](double q, const Onset& onset) { return q < onset.qstamp; });
    const Onset& b = *after;
    const Onset& a = *(after - 1);
    const double span = b.qstamp - a.qstamp;
    if (span <= 0.0)
        return a.x;
    const double t = (qstamp - a.qstamp) / span;
    return a.x + (b.x - a.x) * t;
}

double FlowController::scroll_x(double viewport_w_canvas, double anchor_frac) const
{
    const double playhead = x_at(position_q_);
    const double max_scroll = std::max(0.0, canvas_width_ - viewport_w_canvas);
    return std::clamp(playhead - anchor_frac * viewport_w_canvas, 0.0, max_scroll);
}

FlowController::LitUpdate FlowController::take_lit_update()
{
    LitUpdate update;
    update.reset = lit_reset_pending_;
    lit_reset_pending_ = false;
    while (lit_cursor_ < onsets_.size() && onsets_[lit_cursor_].qstamp <= position_q_)
    {
        const Onset& onset = onsets_[lit_cursor_];
        update.newly_lit.insert(update.newly_lit.end(), onset.ids.begin(), onset.ids.end());
        ++lit_cursor_;
    }
    return update;
}

} // namespace scoreview
} // namespace draxul
