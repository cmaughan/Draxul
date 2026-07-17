#include <draxul/scoreview/stream_composer.h>

#include <draxul/scoreview/measure_xml.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace draxul
{
namespace scoreview
{

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
    last_seam_slot_.clear();
    seams_used_.clear();
    // Re-serve responds to LIVE fumbles: baseline every bar at its current
    // pass count so a program planned from a restored model doesn't open
    // with "fix" slots for last session's history.
    reserved_at_pass_.clear();
    if (model_ != nullptr && slicer_ != nullptr && slicer_->ready())
    {
        for (int bar = 0; bar < slicer_->bar_count(); ++bar)
        {
            const int passes = model_->bar_pass_count(bar);
            if (passes > 0)
                reserved_at_pass_[bar] = passes;
        }
    }
    arc_ = 0;
    arc_start_bar_ = 0;
    arc_end_bar_ = -1;
    arc_on_phrase_ = false;
    performance_run_ = false;
}

int StreamComposer::ensure(StreamProgram& program, int slots)
{
    while (!finished_ && program.size() < slots)
        compose_next(program);
    return program.size();
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
            && model_->bar_consecutive_clean(bar) >= kPromotionCleanPasses;
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
    // Chunk on musical structure, never on a fixed window: practice segments
    // belong on phrase boundaries, and the fixed slice survives only for
    // material whose structure we cannot honestly see.
    if (!begin_weakest_phrase(total))
        begin_weakest_slice(total);
    ++arc_;
}

bool StreamComposer::begin_weakest_phrase(int total)
{
    if (profile_ == nullptr || profile_->phrases.empty()
        || profile_->structure_confidence < kMinStructureConfidence)
        return false;
    const PieceProfile::Phrase* weakest = nullptr;
    double worst = 1e9;
    for (const PieceProfile::Phrase& phrase : profile_->phrases)
    {
        const int begin = std::clamp(phrase.start_bar, 0, total);
        const int end = std::clamp(phrase.end_bar, 0, total);
        if (end <= begin)
            continue;
        double sum = 0.0;
        for (int bar = begin; bar < end; ++bar)
            sum += model_->bar_encounters(bar) > 0 ? model_->bar_mastery(bar) : 0.0;
        // Mean, not sum: phrases differ in length, and a long phrase must not
        // look weak merely for being long.
        const double mean = sum / static_cast<double>(end - begin);
        if (mean < worst)
        {
            worst = mean;
            weakest = &phrase;
        }
    }
    if (weakest == nullptr)
        return false;
    frontier_ = std::clamp(weakest->start_bar, 0, std::max(0, total - 1));
    arc_start_bar_ = frontier_;
    arc_end_bar_ = std::clamp(weakest->end_bar, frontier_ + 1, total);
    arc_on_phrase_ = true;
    return true;
}

void StreamComposer::begin_weakest_slice(int total)
{
    // Fallback only (see kSliceBars): the weakest fixed-length window, with
    // unencountered bars counting as 0.
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
    arc_start_bar_ = best_start;
    arc_end_bar_ = std::min(total, best_start + slice);
    arc_on_phrase_ = false;
}

bool StreamComposer::try_hands(StreamProgram& program, int slot)
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
    plan.source_start_q = slicer_->bar_start_q(worst_bar);
    plan.drill_xml = slicer_->hands_separate_xml(worst_bar, weak_staff);
    if (plan.drill_xml.empty())
        return false;
    plan.reason = "hands separate: bar " + std::to_string(worst_bar + 1) + ", staff "
        + std::to_string(weak_staff) + " alone";
    piece_bars_since_special_ = 0;
    program.append(std::move(plan), slicer_->bar_quarters(worst_bar));
    (void)slot;
    return true;
}

bool StreamComposer::try_drill(StreamProgram& program, int slot)
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
    plan.source_start_q = slicer_->bar_start_q(plan.source_bar);
    plan.drill_xml = fabricate_chord_drill(worst_key, plan.source_bar, broken);
    if (plan.drill_xml.empty())
        return false;
    plan.reason = std::string(broken ? "drill (broken) chord " : "drill chord ") + worst_key
        + " (" + std::to_string(worst_trouble) + " trouble)";
    last_drill_slot_[worst_key] = slot;
    piece_bars_since_special_ = 0;
    const double quarters = slicer_->bar_quarters(plan.source_bar);
    program.append(std::move(plan), quarters);
    return true;
}

bool StreamComposer::try_scale(StreamProgram& program, int slot)
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
    plan.source_start_q = slicer_->bar_start_q(plan.source_bar);
    plan.drill_xml = fabricate_scale_bar(plan.source_bar, worst_window * 12 + 6);
    if (plan.drill_xml.empty())
        return false;
    plan.reason = "scale through the troubled register (midi "
        + std::to_string(worst_window * 12) + ".." + std::to_string(worst_window * 12 + 11)
        + ", " + std::to_string(worst_misses) + " misses)";
    last_scale_slot_[worst_window] = slot;
    piece_bars_since_special_ = 0;
    const double quarters = slicer_->bar_quarters(plan.source_bar);
    program.append(std::move(plan), quarters);
    return true;
}

bool StreamComposer::try_review(StreamProgram& program, int slot)
{
    int worst_bar = -1;
    double worst_mastery = 0.0;
    double best_priority = 0.0;
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
        if (mastery >= kReviewMasteryThreshold)
            continue;
        // Weakness first, but serial position breaks the near-ties: recall
        // collapses toward a phrase's tail, so the later bar earns the slot.
        const double priority = (kReviewMasteryThreshold - mastery)
            + kTailWeight * (profile_ != nullptr ? profile_->bar_tail_fraction(bar) : 0.0);
        if (priority > best_priority)
        {
            best_priority = priority;
            worst_mastery = mastery;
            worst_bar = bar;
        }
    }
    if (worst_bar < 0)
        return false;
    StreamBarPlan plan;
    plan.kind = StreamBarPlan::Kind::Review;
    plan.source_bar = worst_bar;
    plan.source_start_q = slicer_->bar_start_q(worst_bar);
    char mastery_text[32];
    std::snprintf(mastery_text, sizeof(mastery_text), "%.2f", worst_mastery);
    plan.reason = "review bar " + std::to_string(worst_bar + 1) + " (mastery " + mastery_text + ")";
    ++reviews_used_[worst_bar];
    last_review_slot_[worst_bar] = slot;
    piece_bars_since_special_ = 0;
    program.append(std::move(plan), slicer_->bar_quarters(worst_bar));
    return true;
}

bool StreamComposer::try_seam(StreamProgram& program, int slot)
{
    // Hesitations concentrate at the joins between phrases. Arcs practise ONE
    // phrase at a time and stop dead at its end, so the join to the next
    // phrase is the one place arc practice never covers — the two bars are
    // each fine alone and fail together. Serve the pair back-to-back.
    if (profile_ == nullptr || profile_->phrases.size() < 2
        || profile_->structure_confidence < kMinStructureConfidence)
        return false;
    const int total = slicer_->bar_count();
    int best_tail = -1;
    int best_head = -1;
    double worst = kSeamMasteryThreshold;
    for (size_t i = 0; i + 1 < profile_->phrases.size(); ++i)
    {
        const int tail = profile_->phrases[i].end_bar - 1;
        const int head = profile_->phrases[i + 1].start_bar;
        if (tail < 0 || tail >= total || head < 0 || head >= total)
            continue;
        // Both sides must have been met: an unplayed join is the frontier's
        // job, not a repair.
        if (model_->bar_encounters(tail) == 0 || model_->bar_encounters(head) == 0)
            continue;
        if (seams_used_[tail] >= kMaxSeamsPerJoin)
            continue;
        const auto last = last_seam_slot_.find(tail);
        if (last != last_seam_slot_.end() && slot - last->second < kDrillCooldownSlots)
            continue;
        const double quality = 0.5 * (model_->bar_mastery(tail) + model_->bar_mastery(head));
        if (quality < worst)
        {
            worst = quality;
            best_tail = tail;
            best_head = head;
        }
    }
    if (best_tail < 0)
        return false;
    ++seams_used_[best_tail];
    last_seam_slot_[best_tail] = slot;
    piece_bars_since_special_ = 0;
    const std::string join
        = std::to_string(best_tail + 1) + "->" + std::to_string(best_head + 1);
    for (const int bar : { best_tail, best_head })
    {
        StreamBarPlan plan;
        plan.kind = StreamBarPlan::Kind::Review;
        plan.source_bar = bar;
        plan.source_start_q = slicer_->bar_start_q(bar);
        plan.reason = "seam " + join + (bar == best_tail ? ": phrase tail" : ": next phrase head");
        program.append(std::move(plan), slicer_->bar_quarters(bar));
    }
    return true;
}

bool StreamComposer::try_reserve(StreamProgram& program, int slot)
{
    // The evidence: errors are corrected and repeated, never played past. A
    // fumbled traversal brings its bar back at the next planned slot — once
    // per fumbled pass, so a still-failing bar re-serves after each attempt
    // without ever flooding the stream.
    int worst_bar = -1;
    int worst_pass = -1;
    for (int bar = 0; bar < slicer_->bar_count(); ++bar)
    {
        if (!model_->bar_last_pass_dirty(bar))
            continue;
        const int passes = model_->bar_pass_count(bar);
        const auto served = reserved_at_pass_.find(bar);
        if (served != reserved_at_pass_.end() && served->second >= passes)
            continue; // this fumble already earned its re-serve
        if (passes > worst_pass)
        {
            worst_pass = passes;
            worst_bar = bar;
        }
    }
    if (worst_bar < 0)
        return false;
    reserved_at_pass_[worst_bar] = worst_pass;
    StreamBarPlan plan;
    plan.kind = StreamBarPlan::Kind::Review;
    plan.source_bar = worst_bar;
    plan.source_start_q = slicer_->bar_start_q(worst_bar);
    plan.reason = "fix: bar " + std::to_string(worst_bar + 1) + " (fumbled pass)";
    piece_bars_since_special_ = 0;
    program.append(std::move(plan), slicer_->bar_quarters(worst_bar));
    (void)slot;
    return true;
}

void StreamComposer::compose_next(StreamProgram& program)
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

    const int slot = program.size();
    // The error re-serve outranks everything except the performance run —
    // it bypasses the between-specials floor because a correction must not
    // wait its turn behind variety scheduling.
    if (!performance_run_ && try_reserve(program, slot))
    {
        ++specials_count_;
        return;
    }
    const bool specials_allowed = !performance_run_ && piece_bars_since_special_ >= kMinPieceBarsBetweenSpecials;
    if (specials_allowed)
    {
        // Rotate the chain so every trouble type gets airtime — twenty
        // weak bars must not starve the chord drills (never boring).
        using TryFn = bool (StreamComposer::*)(StreamProgram&, int);
        static constexpr TryFn kChain[5] = { &StreamComposer::try_hands,
            &StreamComposer::try_drill, &StreamComposer::try_scale,
            &StreamComposer::try_review, &StreamComposer::try_seam };
        constexpr int kChainLength = static_cast<int>(std::size(kChain));
        for (int at = 0; at < kChainLength; ++at)
        {
            if ((this->*kChain[(specials_count_ + at) % kChainLength])(program, slot))
            {
                ++specials_count_;
                return;
            }
        }
    }

    StreamBarPlan plan;
    plan.kind = StreamBarPlan::Kind::Piece;
    plan.source_bar = frontier_++;
    plan.source_start_q = slicer_->bar_start_q(plan.source_bar);
    if (performance_run_ && plan.source_bar == 0)
        plan.reason = "performance run — every bar mastered";
    else if (arc_ > 0 && !performance_run_ && plan.source_bar == arc_start_bar_)
        plan.reason = "arc " + std::to_string(arc_) + ": weakest "
            + (arc_on_phrase_ ? "phrase" : "slice") + ", bars "
            + std::to_string(arc_start_bar_ + 1) + ".." + std::to_string(arc_end_bar_);
    ++piece_bars_since_special_;
    const double quarters = slicer_->bar_quarters(plan.source_bar);
    program.append(std::move(plan), quarters);
}

std::string StreamComposer::fabricate_chord_drill(
    const std::string& chord_key, int reference_bar, bool broken) const
{
    // The composer resolves the drill's musical context (which pitches, the
    // reference bar's divisions and meter); the shared measure writer emits.
    if (slicer_ == nullptr)
        return {};
    ChordDrillSpec spec;
    spec.pitches = parse_chord_key(chord_key);
    spec.divisions = slicer_->divisions_at(reference_bar);
    spec.beats
        = std::max(1, static_cast<int>(std::lround(slicer_->bar_quarters(reference_bar))));
    spec.broken = broken;
    return chord_drill_measure_xml(spec);
}

std::string StreamComposer::fabricate_scale_bar(int reference_bar, int center_pitch) const
{
    if (slicer_ == nullptr)
        return {};
    ScaleBarSpec spec;
    spec.center_pitch = center_pitch;
    spec.divisions = slicer_->divisions_at(reference_bar);
    spec.beats
        = std::max(1, static_cast<int>(std::lround(slicer_->bar_quarters(reference_bar))));
    // The piece's key (C major fallback when unanalyzed).
    if (profile_ != nullptr)
    {
        spec.tonic_pc = profile_->global_key.tonic_pc;
        spec.minor = profile_->global_key.minor;
    }
    return scale_measure_xml(spec);
}

} // namespace scoreview
} // namespace draxul
