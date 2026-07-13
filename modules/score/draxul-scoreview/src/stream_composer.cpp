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
    last_drill_slot_.clear();
    reviews_used_.clear();
    last_review_slot_.clear();
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

void StreamComposer::compose_next()
{
    if (!ready() || frontier_ >= slicer_->bar_count())
    {
        finished_ = true;
        return;
    }
    const int slot = planned();
    StreamBarPlan plan;
    const bool specials_allowed = piece_bars_since_special_ >= kMinPieceBarsBetweenSpecials;

    // 1. Drill: the most troubled chord (misses + splits outweighing clean
    //    grabs), fabricated from the exact voiced pitches, with a cooldown.
    if (specials_allowed)
    {
        std::string worst_key;
        int worst_trouble = kDrillTroubleThreshold - 1;
        for (const auto& [key, stats] : model_->chord_stats())
        {
            const int trouble = stats.miss + stats.split;
            if (trouble > worst_trouble && trouble > stats.clean)
            {
                const auto last = last_drill_slot_.find(key);
                if (last != last_drill_slot_.end() && slot - last->second < kDrillCooldownSlots)
                    continue;
                worst_trouble = trouble;
                worst_key = key;
            }
        }
        if (!worst_key.empty())
        {
            plan.kind = StreamBarPlan::Kind::Drill;
            plan.source_bar = std::max(0, frontier_ - 1);
            plan.drill_xml = fabricate_chord_drill(worst_key, plan.source_bar);
            plan.reason = "drill chord " + worst_key + " (" + std::to_string(worst_trouble)
                + " trouble)";
            if (!plan.drill_xml.empty())
            {
                last_drill_slot_[worst_key] = slot;
                piece_bars_since_special_ = 0;
                program_.push_back(std::move(plan));
                slot_start_q_.push_back(
                    slot_start_q_.back() + slicer_->bar_quarters(program_.back().source_bar));
                return;
            }
        }
    }

    // 2. Review: the weakest already-encountered earlier bar below the
    //    mastery threshold — the "sliced up" spaced repetition.
    if (specials_allowed)
    {
        int worst_bar = -1;
        double worst_mastery = kReviewMasteryThreshold;
        for (int bar = 0; bar < frontier_; ++bar)
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
        if (worst_bar >= 0)
        {
            plan.kind = StreamBarPlan::Kind::Review;
            plan.source_bar = worst_bar;
            char mastery_text[32];
            std::snprintf(mastery_text, sizeof(mastery_text), "%.2f", worst_mastery);
            plan.reason = "review bar " + std::to_string(worst_bar + 1) + " (mastery "
                + mastery_text + ")";
            ++reviews_used_[worst_bar];
            last_review_slot_[worst_bar] = slot;
            piece_bars_since_special_ = 0;
            program_.push_back(std::move(plan));
            slot_start_q_.push_back(
                slot_start_q_.back() + slicer_->bar_quarters(program_.back().source_bar));
            return;
        }
    }

    // 3. The frontier: the piece, in order.
    plan.kind = StreamBarPlan::Kind::Piece;
    plan.source_bar = frontier_++;
    ++piece_bars_since_special_;
    program_.push_back(std::move(plan));
    slot_start_q_.push_back(
        slot_start_q_.back() + slicer_->bar_quarters(program_.back().source_bar));
}

std::string StreamComposer::fabricate_chord_drill(
    const std::string& chord_key, int reference_bar) const
{
    const std::vector<int> pitches = parse_chord_key(chord_key);
    if (pitches.empty() || slicer_ == nullptr)
        return {};
    const int divisions = slicer_->divisions_at(reference_bar);
    const int beats = std::max(
        1, static_cast<int>(std::lround(slicer_->bar_quarters(reference_bar))));
    const int bar_duration = divisions * beats;

    const int bass = pitches.front();
    std::vector<int> uppers(pitches.begin() + 1, pitches.end());
    if (uppers.empty())
        uppers.push_back(bass); // single-note drill: repeat it in both hands

    // Staff 1 / voice 1: the upper grab repeated on every beat — the drill.
    std::string out = "<measure number=\"1\">";
    for (int beat = 0; beat < beats; ++beat)
    {
        for (size_t at = 0; at < uppers.size(); ++at)
        {
            out += "<note>";
            if (at > 0)
                out += "<chord/>";
            out += pitch_xml(uppers[at]);
            out += "<duration>" + std::to_string(divisions) + "</duration>";
            out += "<voice>1</voice><type>quarter</type><staff>1</staff></note>";
        }
    }
    out += "<backup><duration>" + std::to_string(bar_duration) + "</duration></backup>";

    // Staff 2 / voice 5: the bass held under the whole bar.
    out += "<note>";
    out += pitch_xml(bass);
    out += "<duration>" + std::to_string(bar_duration) + "</duration>";
    out += "<voice>5</voice>";
    if (beats == 3)
        out += "<type>half</type><dot/>";
    else if (beats == 4)
        out += "<type>whole</type>";
    else if (beats == 2)
        out += "<type>half</type>";
    else
        out += "<type>quarter</type>";
    out += "<staff>2</staff></note>";
    out += "</measure>";
    return out;
}

} // namespace scoreview
} // namespace draxul
