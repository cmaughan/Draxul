#include <draxul/scoreview/flow_controller.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <unordered_map>
#include <unordered_set>

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
    gates_.clear();
    reset_gates();
    return true;
}

void FlowController::prepare_gates(const std::function<int(const std::string&)>& pitch_for,
    const std::vector<std::string>& tie_end_ids)
{
    const std::unordered_set<std::string> tie_ends(tie_end_ids.begin(), tie_end_ids.end());
    gates_.clear();
    gates_.reserve(onsets_.size());
    for (const Onset& onset : onsets_)
    {
        Gate gate;
        gate.notes.reserve(onset.ids.size());
        for (const std::string& id : onset.ids)
        {
            GateNote note;
            note.id = id;
            note.pitch = pitch_for ? pitch_for(id) : -1;
            // Unjudgeable pitches degrade to auto so a data gap can never
            // wedge the runner at a gate that cannot be satisfied.
            note.auto_satisfied = note.pitch < 0 || tie_ends.count(id) > 0;
            if (!note.auto_satisfied)
                ++gate.required;
            gate.notes.push_back(std::move(note));
        }
        gates_.push_back(std::move(gate));
    }
    reset_gates();
}

void FlowController::set_mode(TransportMode mode)
{
    if (mode_ == mode)
        return;
    mode_ = mode;
    rewind();
}

void FlowController::reset_gates()
{
    for (Gate& gate : gates_)
    {
        gate.events = 0;
        gate.open = false;
        for (GateNote& note : gate.notes)
            note.verdict = NoteVerdict::Pending;
    }
    armed_ = 0;
    verdict_changes_.clear();
    verdict_reset_pending_ = true;
    stall_seconds_ = 0.0;
    pace_ema_qpm_ = 0.0;
    last_open_t_ = -1.0;
    last_open_q_ = -1.0;
    score_ = 0;
    streak_ = 0;
    miss_count_ = 0;
    roll_resolved_ = 0;
    accuracy_ema_ = kRollStartAccuracy;
    wrong_count_ = 0;
}

void FlowController::advance_armed()
{
    while (armed_ < gates_.size() && gates_[armed_].open)
        ++armed_;
}

double FlowController::next_required_qstamp() const
{
    for (size_t i = armed_; i < gates_.size(); ++i)
    {
        if (!gates_[i].open && gates_[i].required > 0)
            return onsets_[i].qstamp;
    }
    return duration_q_;
}

bool FlowController::waiting() const
{
    return mode_ == TransportMode::Gate && playing_ && gates_ready() && armed_ < gates_.size() && !gates_[armed_].open && gates_[armed_].required > 0 && position_q_ + 1e-9 >= onsets_[armed_].qstamp;
}

std::vector<int> FlowController::armed_required_pitches() const
{
    std::vector<int> pitches;
    if (!gates_ready())
        return pitches;
    if (mode_ == TransportMode::Roll)
    {
        // Every onset whose hit window contains the playhead.
        for (size_t i = roll_resolved_; i < gates_.size(); ++i)
        {
            const double q = onsets_[i].qstamp;
            if (q > position_q_ + kRollEarlyWindowQ)
                break;
            if (q < position_q_ - kRollLateWindowQ)
                continue;
            for (const GateNote& note : gates_[i].notes)
            {
                if (note.verdict == NoteVerdict::Pending && !note.auto_satisfied)
                    pitches.push_back(note.pitch);
            }
        }
        return pitches;
    }
    if (armed_ >= gates_.size())
        return pitches;
    for (const GateNote& note : gates_[armed_].notes)
    {
        if (note.verdict == NoteVerdict::Pending && !note.auto_satisfied)
            pitches.push_back(note.pitch);
    }
    return pitches;
}

void FlowController::judge(const std::vector<PlayerNoteEvent>& events)
{
    if (!gates_ready())
        return;
    if (mode_ == TransportMode::Roll)
    {
        judge_roll(events);
        return;
    }
    if (mode_ != TransportMode::Gate)
        return;
    for (const PlayerNoteEvent& event : events)
    {
        if (armed_ >= gates_.size())
            return;
        Gate& gate = gates_[armed_];
        if (gate.required == 0)
            continue; // auto gates open from advance(); stray input is noise

        // A required pending note matching the pitch is the primary target.
        GateNote* match = nullptr;
        for (GateNote& note : gate.notes)
        {
            if (note.verdict == NoteVerdict::Pending && !note.auto_satisfied && note.pitch == event.midi_pitch)
            {
                match = &note;
                break;
            }
        }
        if (match == nullptr)
        {
            // A re-voiced tie continuation is correct AND free: it neither
            // helps nor hurts the gate's attempt count.
            bool revoiced = false;
            for (GateNote& note : gate.notes)
            {
                if (note.verdict == NoteVerdict::Pending && note.auto_satisfied && note.pitch == event.midi_pitch)
                {
                    note.verdict = NoteVerdict::Correct;
                    verdict_changes_.emplace_back(note.id, NoteVerdict::Correct);
                    revoiced = true;
                    break;
                }
            }
            if (revoiced)
                continue;
        }
        ++gate.events;
        if (match != nullptr)
        {
            match->verdict = NoteVerdict::Correct;
            verdict_changes_.emplace_back(match->id, NoteVerdict::Correct);
        }

        int correct_required = 0;
        for (const GateNote& note : gate.notes)
        {
            if (!note.auto_satisfied && note.verdict == NoteVerdict::Correct)
                ++correct_required;
        }
        // Open on full match, or move on once the player has attempted the
        // gate (as many events as it requires) — the music never stops.
        if (correct_required == gate.required || gate.events >= gate.required)
            open_gate(armed_, correct_required == gate.required, event.t_seconds);
    }
}

void FlowController::judge_roll(const std::vector<PlayerNoteEvent>& events)
{
    for (const PlayerNoteEvent& event : events)
    {
        // The nearest in-window onset still needing this pitch wins.
        GateNote* best_note = nullptr;
        double best_distance = std::numeric_limits<double>::max();
        GateNote* revoiced = nullptr;
        for (size_t i = roll_resolved_; i < gates_.size(); ++i)
        {
            const double q = onsets_[i].qstamp;
            if (q > position_q_ + kRollEarlyWindowQ)
                break;
            if (q < position_q_ - kRollLateWindowQ)
                continue;
            for (GateNote& note : gates_[i].notes)
            {
                if (note.verdict != NoteVerdict::Pending || note.pitch != event.midi_pitch)
                    continue;
                if (note.auto_satisfied)
                {
                    if (revoiced == nullptr)
                        revoiced = &note;
                    continue;
                }
                const double distance = std::abs(q - position_q_);
                if (distance < best_distance)
                {
                    best_distance = distance;
                    best_note = &note;
                }
            }
        }
        if (best_note != nullptr)
        {
            best_note->verdict = NoteVerdict::Correct;
            verdict_changes_.emplace_back(best_note->id, NoteVerdict::Correct);
            ++streak_;
            const double bonus = 1.0 + 0.1 * std::min(streak_, kStreakBonusCap);
            score_ += static_cast<int>(
                std::llround(kBaseGatePoints * bonus * (tempo_qpm_ / marking_qpm_)));
            accuracy_sample(1.0);
            continue;
        }
        if (revoiced != nullptr)
        {
            // Re-voicing a tie continuation is correct and free.
            revoiced->verdict = NoteVerdict::Correct;
            verdict_changes_.emplace_back(revoiced->id, NoteVerdict::Correct);
            continue;
        }
        // Matched nothing: a stray pitch or badly-timed note — the raw
        // material the practice generator will feed on.
        ++wrong_count_;
        streak_ = 0;
        accuracy_sample(0.0);
    }
}

void FlowController::resolve_roll_passed()
{
    while (roll_resolved_ < gates_.size())
    {
        if (position_q_ <= onsets_[roll_resolved_].qstamp + kRollLateWindowQ)
            break; // window still open
        Gate& gate = gates_[roll_resolved_];
        bool any_missed = false;
        for (GateNote& note : gate.notes)
        {
            if (note.verdict != NoteVerdict::Pending)
                continue;
            if (note.auto_satisfied)
            {
                note.verdict = NoteVerdict::Correct;
                verdict_changes_.emplace_back(note.id, NoteVerdict::Correct);
                continue;
            }
            note.verdict = NoteVerdict::Missed;
            verdict_changes_.emplace_back(note.id, NoteVerdict::Missed);
            ++miss_count_;
            any_missed = true;
            accuracy_sample(0.0);
        }
        gate.open = true;
        if (any_missed)
            streak_ = 0;
        adjust_roll_tempo();
        ++roll_resolved_;
    }
}

void FlowController::accuracy_sample(double value)
{
    accuracy_ema_ = kRollAccuracyAlpha * value + (1.0 - kRollAccuracyAlpha) * accuracy_ema_;
}

void FlowController::adjust_roll_tempo()
{
    if (accuracy_ema_ >= kRollSpeedUpThreshold)
        set_tempo_qpm(tempo_qpm_ * (1.0 + kRollTempoUpPerOnset));
    else if (accuracy_ema_ <= kRollSlowDownThreshold)
        set_tempo_qpm(tempo_qpm_ * (1.0 - kRollTempoDownPerOnset));
}

void FlowController::open_gate(size_t index, bool clean, double t_seconds)
{
    Gate& gate = gates_[index];
    for (GateNote& note : gate.notes)
    {
        if (note.verdict != NoteVerdict::Pending)
            continue;
        note.verdict = note.auto_satisfied ? NoteVerdict::Correct : NoteVerdict::Missed;
        if (note.verdict == NoteVerdict::Missed)
            ++miss_count_;
        verdict_changes_.emplace_back(note.id, note.verdict);
    }
    gate.open = true;

    // Pace: the player's demonstrated gate-to-gate speed, eased into the
    // transport tempo within the marking band.
    const double q = onsets_[index].qstamp;
    if (last_open_t_ >= 0.0 && t_seconds > last_open_t_ + 0.05 && q > last_open_q_)
    {
        const double implied = std::clamp(
            (q - last_open_q_) / (t_seconds - last_open_t_) * 60.0, min_tempo_qpm(),
            max_tempo_qpm());
        pace_ema_qpm_ = pace_ema_qpm_ <= 0.0
            ? implied
            : kPaceEmaAlpha * implied + (1.0 - kPaceEmaAlpha) * pace_ema_qpm_;
        set_tempo_qpm(tempo_qpm_ + kTempoEasePerGate * (pace_ema_qpm_ - tempo_qpm_));
    }
    last_open_t_ = t_seconds;
    last_open_q_ = q;

    if (clean)
    {
        ++streak_;
        const double bonus = 1.0 + 0.1 * std::min(streak_, kStreakBonusCap);
        score_ += static_cast<int>(
            std::llround(kBaseGatePoints * bonus * (tempo_qpm_ / marking_qpm_)));
    }
    else
    {
        streak_ = 0;
    }
    stall_seconds_ = 0.0;
    advance_armed();
}

FlowController::VerdictUpdate FlowController::take_verdict_update()
{
    VerdictUpdate update;
    update.reset = verdict_reset_pending_;
    verdict_reset_pending_ = false;
    update.changes = std::move(verdict_changes_);
    verdict_changes_.clear();
    return update;
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
    reset_gates();
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
    if (mode_ == TransportMode::Clock || !gates_ready())
    {
        position_q_ += wall_dt_seconds * tempo_qpm_ / 60.0;
        if (position_q_ >= duration_q_)
        {
            position_q_ = duration_q_;
            playing_ = false;
        }
        return;
    }
    if (mode_ == TransportMode::Roll)
    {
        // The runner never waits: roll like the clock, then close any hit
        // windows the playhead has passed (pending notes turn Missed). The
        // piece only finishes once the LAST window has closed — the final
        // notes get their full late window like every other.
        position_q_ += wall_dt_seconds * tempo_qpm_ / 60.0;
        resolve_roll_passed();
        if (position_q_ >= duration_q_ + kRollLateWindowQ)
        {
            position_q_ = duration_q_;
            playing_ = false;
        }
        return;
    }

    // Gate: glide toward the next gate that needs the player, opening any
    // auto-satisfied gates (tie continuations) the playhead crosses.
    const double target = next_required_qstamp();
    position_q_ = std::min(position_q_ + wall_dt_seconds * tempo_qpm_ / 60.0, target);
    while (armed_ < gates_.size() && !gates_[armed_].open && gates_[armed_].required == 0 && onsets_[armed_].qstamp <= position_q_ + 1e-9)
    {
        for (GateNote& note : gates_[armed_].notes)
        {
            if (note.verdict == NoteVerdict::Pending)
            {
                note.verdict = NoteVerdict::Correct;
                verdict_changes_.emplace_back(note.id, NoteVerdict::Correct);
            }
        }
        gates_[armed_].open = true;
        advance_armed();
    }

    if (waiting())
    {
        stall_seconds_ += wall_dt_seconds;
        if (stall_seconds_ > kStallGraceSeconds)
            set_tempo_qpm(tempo_qpm_ * (1.0 - kStallDecayPerSecond * wall_dt_seconds));
    }

    if (armed_ >= gates_.size() && position_q_ >= duration_q_)
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
