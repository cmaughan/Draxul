#include <draxul/scoreview/stream_composer.h>

#include <algorithm>
#include <cmath>

namespace draxul
{
namespace scoreview
{

namespace
{

constexpr const char* kStepNames[12] = { "C", "C", "D", "D", "E", "F", "F", "G", "G", "A",
    "A", "B" };
constexpr int kStepAlters[12] = { 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0 };

std::string pitch_xml(int midi)
{
    const int pc = ((midi % 12) + 12) % 12;
    std::string out = "<pitch><step>";
    out += kStepNames[pc];
    out += "</step>";
    if (kStepAlters[pc] != 0)
        out += "<alter>1</alter>";
    out += "<octave>" + std::to_string(midi / 12 - 1) + "</octave></pitch>";
    return out;
}

std::string note_xml(int midi, int duration, const char* type, int voice, int staff,
    bool chord = false)
{
    std::string out = "<note>";
    if (chord)
        out += "<chord/>";
    out += pitch_xml(midi);
    out += "<duration>" + std::to_string(duration) + "</duration>";
    out += "<voice>" + std::to_string(voice) + "</voice>";
    out += "<type>";
    out += type;
    out += "</type><staff>" + std::to_string(staff) + "</staff></note>";
    return out;
}

std::vector<int> parse_chord_key(const std::string& key)
{
    std::vector<int> pitches;
    size_t at = 0;
    while (at < key.size())
    {
        const size_t plus = key.find('+', at);
        const std::string token = key.substr(at, plus == std::string::npos ? plus : plus - at);
        if (!token.empty())
            pitches.push_back(std::stoi(token));
        if (plus == std::string::npos)
            break;
        at = plus + 1;
    }
    std::sort(pitches.begin(), pitches.end());
    return pitches;
}

} // namespace

void StreamComposer::configure(
    const SourceSlicer* slicer, const PlayerModel* model, const PieceProfile* profile)
{
    slicer_ = slicer;
    model_ = model;
    profile_ = profile;
    reset();
}

void StreamComposer::reset()
{
    program_.clear();
    slot_start_q_.assign(1, 0.0);
    frontier_ = 0;
    finished_ = false;
    piece_bars_since_special_ = 0;
    specials_count_ = 0;
    last_drill_slot_.clear();
    drill_stage_.clear();
    reviews_used_.clear();
    last_review_slot_.clear();
    hands_done_.clear();
    last_scale_slot_.clear();
    arc_ = 0;
    arc_end_bar_ = -1;
    performance_run_ = false;
}

int StreamComposer::ensure(int slots)
{
    while (!finished_ && planned() < slots)
        compose_next();
    return planned();
}

double StreamComposer::slot_start_q(int slot) const
{
    if (slot_start_q_.empty())
        return 0.0;
    const int clamped = std::clamp(slot, 0, static_cast<int>(slot_start_q_.size()) - 1);
    return slot_start_q_[static_cast<size_t>(clamped)];
}

double StreamComposer::slot_quarters(int slot) const
{
    return slot_start_q(slot + 1) - slot_start_q(slot);
}

int StreamComposer::slot_at(double stream_q) const
{
    const int slots = planned();
    for (int slot = 0; slot < slots; ++slot)
    {
        if (stream_q < slot_start_q_[static_cast<size_t>(slot) + 1])
            return slot;
    }
    return slots > 0 ? slots - 1 : 0;
}

void StreamComposer::begin_next_arc()
{
    // Mastery-gated, never time-gated: the stream only converges on the
    // full piece when every bar has EARNED it (encountered and promoted);
    // otherwise it loops through the weakest slice, endlessly.
    const int total = slicer_->bar_count();
    bool all_promoted = true;
    for (int bar = 0; bar < total && all_promoted; ++bar)
    {
        all_promoted = model_->bar_encounters(bar) > 0
            && model_->bar_mastery(bar) >= kPromotionMastery;
    }
    if (all_promoted)
    {
        if (performance_run_)
        {
            finished_ = true; // the earned performance has been scheduled
            return;
        }
        performance_run_ = true;
        frontier_ = 0;
        arc_end_bar_ = total;
        ++arc_;
        return;
    }
    performance_run_ = false;
    // The weakest kSliceBars-long slice (unencountered bars count as 0).
    int best_start = 0;
    double best_mastery = 1e9;
    const int slice = std::min(kSliceBars, total);
    for (int start = 0; start + slice <= total; ++start)
    {
        double sum = 0.0;
        for (int bar = start; bar < start + slice; ++bar)
            sum += model_->bar_encounters(bar) > 0 ? model_->bar_mastery(bar) : 0.0;
        if (sum < best_mastery)
        {
            best_mastery = sum;
            best_start = start;
        }
    }
    frontier_ = best_start;
    arc_end_bar_ = std::min(total, best_start + slice);
    ++arc_;
}

bool StreamComposer::try_hands(int slot)
{
    // The deepest simplification rung: the worst deeply-struggling bar
    // returns with its WEAK HAND ALONE (once per bar per program).
    int worst_bar = -1;
    double worst_mastery = kHandsSeparateMastery;
    for (int bar = 0; bar < slicer_->bar_count(); ++bar)
    {
        if (model_->bar_encounters(bar) == 0 || hands_done_[bar])
            continue;
        const double mastery = model_->bar_mastery(bar);
        if (mastery < worst_mastery)
        {
            worst_mastery = mastery;
            worst_bar = bar;
        }
    }
    if (worst_bar < 0)
        return false;
    hands_done_[worst_bar] = true;
    const auto staves = slicer_->staff_pitches(worst_bar);
    int weak_staff = 1;
    double worst_misses = -1.0;
    for (const auto& [staff, pitches] : staves)
    {
        double misses = 0.0;
        for (const int pitch : pitches)
        {
            const auto found = model_->pitch_stats().find(pitch);
            if (found != model_->pitch_stats().end())
                misses += found->second.miss;
        }
        if (misses > worst_misses)
        {
            worst_misses = misses;
            weak_staff = staff;
        }
    }
    StreamBarPlan plan;
    plan.kind = StreamBarPlan::Kind::Drill;
    plan.source_bar = worst_bar;
    plan.drill_xml = slicer_->hands_separate_xml(worst_bar, weak_staff);
    if (plan.drill_xml.empty())
        return false;
    plan.reason = "hands separate: bar " + std::to_string(worst_bar + 1) + ", staff "
        + std::to_string(weak_staff) + " alone";
    piece_bars_since_special_ = 0;
    program_.push_back(std::move(plan));
    slot_start_q_.push_back(slot_start_q_.back() + slicer_->bar_quarters(worst_bar));
    (void)slot;
    return true;
}

bool StreamComposer::try_drill(int slot)
{
    std::string worst_key;
    int worst_trouble = kDrillTroubleThreshold - 1;
    for (const auto& [key, stats] : model_->chord_stats())
    {
        // Clean grabs retire the drill: trouble is net of successes.
        const int trouble = stats.miss + stats.split - stats.clean;
        if (trouble > worst_trouble)
        {
            const auto last = last_drill_slot_.find(key);
            if (last != last_drill_slot_.end() && slot - last->second < kDrillCooldownSlots)
                continue;
            worst_trouble = trouble;
            worst_key = key;
        }
    }
    if (worst_key.empty())
        return false;
    // The ladder climbs: broken (arpeggiated) first, the block grab after.
    const int stage = drill_stage_[worst_key]++;
    const bool broken = stage == 0;
    StreamBarPlan plan;
    plan.kind = StreamBarPlan::Kind::Drill;
    plan.source_bar = std::clamp(frontier_, 0, slicer_->bar_count() - 1);
    plan.drill_xml = fabricate_chord_drill(worst_key, plan.source_bar, broken);
    if (plan.drill_xml.empty())
        return false;
    plan.reason = std::string(broken ? "drill (broken) chord " : "drill chord ") + worst_key
        + " (" + std::to_string(worst_trouble) + " trouble)";
    last_drill_slot_[worst_key] = slot;
    piece_bars_since_special_ = 0;
    program_.push_back(std::move(plan));
    slot_start_q_.push_back(
        slot_start_q_.back() + slicer_->bar_quarters(program_.back().source_bar));
    return true;
}

bool StreamComposer::try_scale(int slot)
{
    // Register trouble: missed pitches piling up inside an octave window
    // earn a scale fragment through that register, in the piece's key.
    int worst_window = -1;
    int worst_misses = kScaleTroubleThreshold - 1;
    std::map<int, std::pair<int, int>> windows; // octave window -> (miss, hit)
    for (const auto& [pitch, stats] : model_->pitch_stats())
    {
        auto& [miss, hit] = windows[pitch / 12];
        miss += stats.miss;
        hit += stats.hit;
    }
    for (const auto& [window, counts] : windows)
    {
        if (counts.first > worst_misses && counts.first > counts.second)
        {
            const auto last = last_scale_slot_.find(window);
            if (last != last_scale_slot_.end() && slot - last->second < kDrillCooldownSlots)
                continue;
            worst_misses = counts.first;
            worst_window = window;
        }
    }
    if (worst_window < 0)
        return false;
    StreamBarPlan plan;
    plan.kind = StreamBarPlan::Kind::Drill;
    plan.source_bar = std::clamp(frontier_, 0, slicer_->bar_count() - 1);
    plan.drill_xml = fabricate_scale_bar(plan.source_bar, worst_window * 12 + 6);
    if (plan.drill_xml.empty())
        return false;
    plan.reason = "scale through the troubled register (midi "
        + std::to_string(worst_window * 12) + ".." + std::to_string(worst_window * 12 + 11)
        + ", " + std::to_string(worst_misses) + " misses)";
    last_scale_slot_[worst_window] = slot;
    piece_bars_since_special_ = 0;
    program_.push_back(std::move(plan));
    slot_start_q_.push_back(
        slot_start_q_.back() + slicer_->bar_quarters(program_.back().source_bar));
    return true;
}

bool StreamComposer::try_review(int slot)
{
    int worst_bar = -1;
    double worst_mastery = kReviewMasteryThreshold;
    for (int bar = 0; bar < slicer_->bar_count(); ++bar)
    {
        if (model_->bar_encounters(bar) == 0)
            continue;
        if (reviews_used_[bar] >= kMaxReviewsPerBar)
            continue;
        const auto last = last_review_slot_.find(bar);
        if (last != last_review_slot_.end() && slot - last->second < kDrillCooldownSlots)
            continue;
        const double mastery = model_->bar_mastery(bar);
        if (mastery < worst_mastery)
        {
            worst_mastery = mastery;
            worst_bar = bar;
        }
    }
    if (worst_bar < 0)
        return false;
    StreamBarPlan plan;
    plan.kind = StreamBarPlan::Kind::Review;
    plan.source_bar = worst_bar;
    char mastery_text[32];
    std::snprintf(mastery_text, sizeof(mastery_text), "%.2f", worst_mastery);
    plan.reason = "review bar " + std::to_string(worst_bar + 1) + " (mastery " + mastery_text + ")";
    ++reviews_used_[worst_bar];
    last_review_slot_[worst_bar] = slot;
    piece_bars_since_special_ = 0;
    program_.push_back(std::move(plan));
    slot_start_q_.push_back(
        slot_start_q_.back() + slicer_->bar_quarters(program_.back().source_bar));
    return true;
}

void StreamComposer::compose_next()
{
    if (!ready())
    {
        finished_ = true;
        return;
    }
    const int total = slicer_->bar_count();
    if (frontier_ >= (arc_end_bar_ < 0 ? total : arc_end_bar_))
    {
        begin_next_arc();
        if (finished_)
            return;
    }

    const int slot = planned();
    const bool specials_allowed = !performance_run_ && piece_bars_since_special_ >= kMinPieceBarsBetweenSpecials;
    if (specials_allowed)
    {
        // Rotate the chain so every trouble type gets airtime — twenty
        // weak bars must not starve the chord drills (never boring).
        using TryFn = bool (StreamComposer::*)(int);
        static constexpr TryFn kChain[4] = { &StreamComposer::try_hands,
            &StreamComposer::try_drill, &StreamComposer::try_scale,
            &StreamComposer::try_review };
        for (int at = 0; at < 4; ++at)
        {
            if ((this->*kChain[(specials_count_ + at) % 4])(slot))
            {
                ++specials_count_;
                return;
            }
        }
    }

    StreamBarPlan plan;
    plan.kind = StreamBarPlan::Kind::Piece;
    plan.source_bar = frontier_++;
    if (performance_run_ && plan.source_bar == 0)
        plan.reason = "performance run — every bar mastered";
    else if (arc_ > 0 && !performance_run_ && frontier_ - 1 == arc_end_bar_ - std::min(kSliceBars, total))
        plan.reason = "arc " + std::to_string(arc_) + ": weakest slice, bars "
            + std::to_string(frontier_) + ".." + std::to_string(arc_end_bar_);
    ++piece_bars_since_special_;
    program_.push_back(std::move(plan));
    slot_start_q_.push_back(
        slot_start_q_.back() + slicer_->bar_quarters(program_.back().source_bar));
}

std::string StreamComposer::fabricate_chord_drill(
    const std::string& chord_key, int reference_bar, bool broken) const
{
    const std::vector<int> pitches = parse_chord_key(chord_key);
    if (pitches.empty() || slicer_ == nullptr)
        return {};
    const int divisions = slicer_->divisions_at(reference_bar);
    const int beats = std::max(1, static_cast<int>(std::lround(slicer_->bar_quarters(reference_bar))));
    const int bar_duration = divisions * beats;

    const int bass = pitches.front();
    std::vector<int> uppers(pitches.begin() + 1, pitches.end());
    if (uppers.empty())
        uppers.push_back(bass); // single-note drill: repeat it in both hands

    std::string out = "<measure number=\"1\">";
    if (broken && divisions % 2 == 0 && beats >= 2)
    {
        // Broken form: arpeggiate the grab as eighths, land the block chord
        // on the final beat — the easier rung of the ladder.
        const int eighth = divisions / 2;
        const int broken_beats = beats - 1;
        for (int at = 0; at < broken_beats * 2; ++at)
            out += note_xml(uppers[static_cast<size_t>(at) % uppers.size()], eighth,
                "eighth", 1, 1);
        for (size_t at = 0; at < uppers.size(); ++at)
            out += note_xml(uppers[at], divisions, "quarter", 1, 1, at > 0);
    }
    else
    {
        // Block form: the grab repeated on every beat.
        for (int beat = 0; beat < beats; ++beat)
        {
            for (size_t at = 0; at < uppers.size(); ++at)
                out += note_xml(uppers[at], divisions, "quarter", 1, 1, at > 0);
        }
    }
    out += "<backup><duration>" + std::to_string(bar_duration) + "</duration></backup>";
    out += "<note>";
    out += pitch_xml(bass);
    out += "<duration>" + std::to_string(bar_duration) + "</duration><voice>5</voice>";
    if (beats == 3)
        out += "<type>half</type><dot/>";
    else if (beats == 4)
        out += "<type>whole</type>";
    else if (beats == 2)
        out += "<type>half</type>";
    else
        out += "<type>quarter</type>";
    out += "<staff>2</staff></note></measure>";
    return out;
}

std::string StreamComposer::fabricate_scale_bar(int reference_bar, int center_pitch) const
{
    if (slicer_ == nullptr)
        return {};
    const int divisions = slicer_->divisions_at(reference_bar);
    const int beats = std::max(1, static_cast<int>(std::lround(slicer_->bar_quarters(reference_bar))));

    // The piece's key (C major fallback when unanalyzed).
    int tonic_pc = 0;
    bool minor = false;
    if (profile_ != nullptr)
    {
        tonic_pc = profile_->global_key.tonic_pc;
        minor = profile_->global_key.minor;
    }
    static const int kMajor[7] = { 0, 2, 4, 5, 7, 9, 11 };
    static const int kMinor[7] = { 0, 2, 3, 5, 7, 8, 10 };
    const int* degrees = minor ? kMinor : kMajor;

    // Start on the tonic nearest below the troubled register's center.
    int start = tonic_pc;
    while (start + 12 <= center_pitch - 6)
        start += 12;

    const bool eighths = divisions % 2 == 0 && beats >= 2;
    const int count = eighths ? beats * 2 : beats;
    const int duration = eighths ? divisions / 2 : divisions;
    const char* type = eighths ? "eighth" : "quarter";

    std::string out = "<measure number=\"1\">";
    for (int at = 0; at < count; ++at)
    {
        const int degree = at % 7;
        const int octave_up = (at / 7) * 12;
        out += note_xml(start + degrees[degree] + octave_up, duration, type, 1, 1);
    }
    out += "</measure>";
    return out;
}

} // namespace scoreview
} // namespace draxul
