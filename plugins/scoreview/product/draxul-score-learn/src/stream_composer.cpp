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
    drill_cooldown_.clear();
    drill_stage_.clear();
    reviews_used_.clear();
    review_cooldown_.clear();
    hands_served_at_pass_.clear();
    scale_cooldown_.clear();
    seam_cooldown_.clear();
    seams_used_.clear();
    // Re-serve responds to LIVE fumbles: baseline every bar at its current
    // pass count so a program planned from a restored model doesn't open
    // with "fix" slots for last session's history.
    reserved_at_pass_.clear();
    opening_queue_.clear();
    if (model_ != nullptr && slicer_ != nullptr && slicer_->ready())
    {
        for (int bar = 0; bar < slicer_->bar_count(); ++bar)
        {
            const int passes = model_->bar_pass_count(bar);
            if (passes > 0)
                reserved_at_pass_[bar] = passes;
        }
        // C4: the session opening. Two kinds of time-due bars, from the
        // evidence base:
        //  - overnight re-tests: bars FUMBLED on an earlier day. Sleep
        //    selectively consolidates the hardest transitions, so yesterday's
        //    problem points are re-tested first — expected to have improved
        //    without practice — rather than drilled from cold.
        //  - spaced reviews: clean bars whose day gap reached the expanding
        //    schedule (1/3/7/14/30 days by day-separated clean streak).
        const int today = model_->current_day();
        if (today > 0)
        {
            std::vector<OpeningReview> retests;
            std::vector<OpeningReview> due;
            for (int bar = 0; bar < slicer_->bar_count(); ++bar)
            {
                const int last_day = model_->bar_last_pass_day(bar);
                if (last_day <= 0 || last_day >= today)
                    continue; // never played, or already seen today
                OpeningReview review;
                review.bar = bar;
                review.gap_days = today - last_day;
                if (model_->bar_last_pass_dirty(bar))
                {
                    review.overnight_retest = true;
                    retests.push_back(review);
                }
                else
                {
                    const int streak = std::min(model_->bar_spaced_streak(bar),
                        static_cast<int>(std::size(kSpacedIntervalsDays)) - 1);
                    if (review.gap_days >= kSpacedIntervalsDays[streak])
                        due.push_back(review);
                }
            }
            // Most-overdue first within each kind; re-tests outrank reviews.
            const auto by_gap = [](const OpeningReview& a, const OpeningReview& b) {
                return a.gap_days != b.gap_days ? a.gap_days > b.gap_days : a.bar < b.bar;
            };
            std::sort(retests.begin(), retests.end(), by_gap);
            std::sort(due.begin(), due.end(), by_gap);
            for (const OpeningReview& review : retests)
            {
                if (opening_queue_.size() < static_cast<size_t>(kMaxOpeningReviews))
                    opening_queue_.push_back(review);
            }
            for (const OpeningReview& review : due)
            {
                if (opening_queue_.size() < static_cast<size_t>(kMaxOpeningReviews))
                    opening_queue_.push_back(review);
            }
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
    // The deepest simplification rung: when one engraved hand keeps fumbling,
    // isolate just that hand until it earns a clean source-mapped pass.
    int worst_bar = -1;
    int weak_staff = 1;
    double worst_trouble = 0.0;
    for (int bar = 0; bar < slicer_->bar_count(); ++bar)
    {
        if (model_->bar_encounters(bar) == 0)
            continue;
        const double mastery = model_->bar_mastery(bar);
        const auto tally = model_->bar_tally().find(bar);
        if (tally == model_->bar_tally().end())
            continue;
        const auto staves = slicer_->staff_pitches(bar);
        for (const int staff : { 1, 2 })
        {
            if (staves.find(staff) == staves.end())
                continue;
            const PlayerModel::HandTally& hand = staff == 2 ? tally->second.left : tally->second.right;
            const bool repeated_hand_trouble = hand.miss >= kHandsSeparateTroubleThreshold
                && hand.miss >= hand.hit;
            const bool deeply_struggling_hand = mastery < kHandsSeparateMastery && hand.miss > 0;
            if ((!repeated_hand_trouble && !deeply_struggling_hand)
                || model_->bar_hand_consecutive_clean(bar, staff) > 0)
                continue;
            const int pass_count = model_->bar_hand_pass_count(bar, staff);
            const int key = bar * 10 + staff;
            const auto served = hands_served_at_pass_.find(key);
            if (served != hands_served_at_pass_.end() && served->second == pass_count)
                continue;
            const double trouble = static_cast<double>(hand.miss - hand.hit)
                + (model_->bar_hand_last_pass_dirty(bar, staff) ? 100.0 : 0.0)
                + std::max(0.0, kHandsSeparateMastery - mastery) * 10.0;
            if (trouble > worst_trouble)
            {
                worst_trouble = trouble;
                worst_bar = bar;
                weak_staff = staff;
            }
        }
    }
    if (worst_bar < 0)
        return false;
    StreamBarPlan plan = make_hands_plan(worst_bar, weak_staff, "hands separate");
    if (plan.drill_xml.empty())
        return false;
    hands_served_at_pass_[worst_bar * 10 + weak_staff]
        = model_->bar_hand_pass_count(worst_bar, weak_staff);
    piece_bars_since_special_ = 0;
    append_fabricated(program, *slicer_, worst_bar, std::move(plan.drill_xml),
        std::move(plan.reason), /*trains_source=*/true);
    (void)slot;
    return true;
}

StreamBarPlan StreamComposer::make_hands_plan(
    int bar, int staff, const std::string& reason_prefix) const
{
    StreamBarPlan plan;
    plan.kind = StreamBarPlan::Kind::Drill;
    plan.source_bar = bar;
    plan.source_start_q = slicer_->bar_start_q(bar);
    plan.drill_trains_source = true;
    plan.drill_xml = slicer_->hands_separate_xml(bar, staff);
    const char* hand = staff == 2 ? "left hand" : "right hand";
    plan.reason = reason_prefix + ": bar " + std::to_string(bar + 1) + ", " + hand
        + " alone";
    return plan;
}

bool StreamComposer::try_drill(StreamProgram& program, int slot)
{
    if (!drills_enabled_)
        return false;
    std::string worst_key;
    int worst_trouble = kDrillTroubleThreshold - 1;
    for (const auto& [key, stats] : model_->chord_stats())
    {
        // Clean grabs retire the drill: trouble is net of successes.
        const int trouble = stats.miss + stats.split - stats.clean;
        if (trouble > worst_trouble)
        {
            if (!drill_cooldown_.ready(key, slot))
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
    const int reference_bar = std::clamp(frontier_, 0, slicer_->bar_count() - 1);
    std::string xml = fabricate_chord_drill(worst_key, reference_bar, broken);
    if (xml.empty())
        return false;
    std::string reason = std::string(broken ? "drill (broken) chord " : "drill chord ") + worst_key
        + " (" + std::to_string(worst_trouble) + " trouble)";
    drill_cooldown_.note(worst_key, slot);
    piece_bars_since_special_ = 0;
    append_fabricated(program, *slicer_, reference_bar, std::move(xml), std::move(reason));
    return true;
}

bool StreamComposer::try_scale(StreamProgram& program, int slot)
{
    if (!scales_enabled_)
        return false;
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
            if (!scale_cooldown_.ready(window, slot))
                continue;
            worst_misses = counts.first;
            worst_window = window;
        }
    }
    if (worst_window < 0)
        return false;
    const int reference_bar = std::clamp(frontier_, 0, slicer_->bar_count() - 1);
    std::string xml = fabricate_scale_bar(reference_bar, worst_window * 12 + 6);
    if (xml.empty())
        return false;
    std::string reason = "scale through the troubled register (midi "
        + std::to_string(worst_window * 12) + ".." + std::to_string(worst_window * 12 + 11)
        + ", " + std::to_string(worst_misses) + " misses)";
    scale_cooldown_.note(worst_window, slot);
    piece_bars_since_special_ = 0;
    append_fabricated(program, *slicer_, reference_bar, std::move(xml), std::move(reason));
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
        if (!review_cooldown_.ready(bar, slot))
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
    char mastery_text[32];
    std::snprintf(mastery_text, sizeof(mastery_text), "%.2f", worst_mastery);
    std::string reason
        = "review bar " + std::to_string(worst_bar + 1) + " (mastery " + mastery_text + ")";
    ++reviews_used_[worst_bar];
    review_cooldown_.note(worst_bar, slot);
    piece_bars_since_special_ = 0;
    append_source_bar(program, *slicer_, StreamBarPlan::Kind::Review, worst_bar, std::move(reason));
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
        if (!seam_cooldown_.ready(tail, slot))
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
    seam_cooldown_.note(best_tail, slot);
    piece_bars_since_special_ = 0;
    const std::string join
        = std::to_string(best_tail + 1) + "->" + std::to_string(best_head + 1);
    for (const int bar : { best_tail, best_head })
    {
        append_source_bar(program, *slicer_, StreamBarPlan::Kind::Review, bar,
            "seam " + join + (bar == best_tail ? ": phrase tail" : ": next phrase head"));
    }
    return true;
}

int StreamComposer::find_fix_bar() const
{
    // The newest fumbled traversal that hasn't earned its correction yet —
    // once per fumbled pass, so a still-failing bar re-serves after each
    // attempt without ever flooding the stream.
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
    return worst_bar;
}

int StreamComposer::weak_hand_for_fix(int bar) const
{
    const auto tally = model_->bar_tally().find(bar);
    if (tally == model_->bar_tally().end())
        return -1;
    const auto staves = slicer_->staff_pitches(bar);
    int best_staff = -1;
    int best_trouble = 0;
    for (const int staff : { 1, 2 })
    {
        if (staves.find(staff) == staves.end() || !model_->bar_hand_last_pass_dirty(bar, staff)
            || model_->bar_hand_consecutive_clean(bar, staff) > 0)
            continue;
        const PlayerModel::HandTally& hand = staff == 2 ? tally->second.left : tally->second.right;
        if (hand.miss < kHandsSeparateTroubleThreshold || hand.miss < hand.hit)
            continue;
        const int trouble = hand.miss - hand.hit;
        if (trouble > best_trouble)
        {
            best_trouble = trouble;
            best_staff = staff;
        }
    }
    return best_staff;
}

bool StreamComposer::try_reserve(StreamProgram& program, int slot)
{
    // The evidence: errors are corrected and repeated, never played past. The
    // append path — the splice path (plan_urgent) usually claims the fix
    // first; this catches fumbles when rewriting isn't available.
    const int bar = find_fix_bar();
    if (bar < 0)
        return false;
    reserved_at_pass_[bar] = model_->bar_pass_count(bar);
    piece_bars_since_special_ = 0;
    const int staff = weak_hand_for_fix(bar);
    if (staff > 0)
    {
        StreamBarPlan hands_plan = make_hands_plan(bar, staff, "fix");
        if (!hands_plan.drill_xml.empty())
        {
            hands_served_at_pass_[bar * 10 + staff] = model_->bar_hand_pass_count(bar, staff);
            append_fabricated(program, *slicer_, bar, std::move(hands_plan.drill_xml),
                hands_plan.reason + " (fumbled pass)", /*trains_source=*/true);
            (void)slot;
            return true;
        }
    }
    append_source_bar(program, *slicer_, StreamBarPlan::Kind::Review, bar,
        "fix: bar " + std::to_string(bar + 1) + " (fumbled pass)");
    (void)slot;
    return true;
}

int StreamComposer::plan_urgent(StreamProgram& program, int at_slot)
{
    // The rewriting composer's first move: a fumble's correction is SPLICED
    // just past the playhead instead of waiting a full engrave window. The
    // caller guards the splice point; here we keep the plan consistent:
    // every slot-indexed cooldown at or past the splice shifts by one.
    if (!ready() || finished_ || performance_run_)
        return 0;
    const int bar = find_fix_bar();
    if (bar < 0)
        return 0;
    reserved_at_pass_[bar] = model_->bar_pass_count(bar);
    const int staff = weak_hand_for_fix(bar);
    bool spliced = false;
    if (staff > 0)
    {
        StreamBarPlan hands_plan = make_hands_plan(bar, staff, "fix now");
        if (!hands_plan.drill_xml.empty())
        {
            hands_served_at_pass_[bar * 10 + staff] = model_->bar_hand_pass_count(bar, staff);
            insert_fabricated(program, *slicer_, at_slot, bar, std::move(hands_plan.drill_xml),
                hands_plan.reason + " (fumbled pass)", /*trains_source=*/true);
            spliced = true;
        }
    }
    if (!spliced)
    {
        insert_source_bar(program, *slicer_, at_slot, StreamBarPlan::Kind::Review, bar,
            "fix now: bar " + std::to_string(bar + 1) + " (fumbled pass)");
    }
    // Every slot-indexed cooldown at or past the splice shifts by one, in one
    // loop over the registered list — a cooldown a composer adds is shifted
    // automatically, so the plan can never silently desync after a rewrite.
    for (ISlotShift* cooldown : slot_cooldowns_)
        cooldown->shift_from(at_slot);
    return 1;
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
    // The session opening drains first: overnight re-tests of yesterday's
    // problem points, then time-due spaced reviews.
    if (!performance_run_ && !opening_queue_.empty())
    {
        const OpeningReview review = opening_queue_.front();
        opening_queue_.erase(opening_queue_.begin());
        std::string reason = review.overnight_retest
            ? "overnight re-test: bar " + std::to_string(review.bar + 1) + " ("
                + std::to_string(review.gap_days) + "d since fumble)"
            : "spaced review: bar " + std::to_string(review.bar + 1) + " (gap "
                + std::to_string(review.gap_days) + "d)";
        piece_bars_since_special_ = 0;
        append_source_bar(
            program, *slicer_, StreamBarPlan::Kind::Review, review.bar, std::move(reason));
        return;
    }
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

    const int piece_bar = frontier_++;
    std::string reason;
    if (performance_run_ && piece_bar == 0)
        reason = "performance run — every bar mastered";
    else if (arc_ > 0 && !performance_run_ && piece_bar == arc_start_bar_)
        reason = "arc " + std::to_string(arc_) + ": weakest "
            + (arc_on_phrase_ ? "phrase" : "slice") + ", bars "
            + std::to_string(arc_start_bar_ + 1) + ".." + std::to_string(arc_end_bar_);
    ++piece_bars_since_special_;
    append_source_bar(program, *slicer_, StreamBarPlan::Kind::Piece, piece_bar, std::move(reason));
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
