#include "score_stream_controller.h"

#include <draxul/log.h>

#include <algorithm>

namespace draxul
{
namespace scoreview
{

ScoreStreamController::ScoreStreamController()
    : composer_(std::make_unique<StreamComposer>())
{
}

void ScoreStreamController::reset_plan()
{
    program_.clear();
    composer_->reset();
    last_logged_plan_slot_ = -1;
}

double ScoreStreamController::window_end_q(int first_bar, int count) const
{
    return composing_ ? program_.slot_start_q(first_bar + count)
                      : slicer_.bar_start_q(first_bar + count);
}

bool ScoreStreamController::try_urgent_rewrite(double stream_q)
{
    if (!composing_ || composer_ == nullptr)
        return false;
    // Splice past the playhead's slot plus ONE guard bar: the background
    // engrave takes ~100-200ms, far less than a bar, so the guard bar is the
    // only slot that could play from the OLD window while the new one builds
    // — and its plan is unchanged.
    const int playhead_slot = program_.slot_at(stream_q);
    const int splice = std::min(playhead_slot + 2, program_.size());
    return composer_->plan_urgent(program_, splice) > 0;
}

std::optional<ScoreStreamController::WindowSlice> ScoreStreamController::build_window_slice(
    int first_bar)
{
    // The cheap main-thread half of a window advance (slicing + composer
    // planning); the heavy Verovio engrave then runs sync or on the worker.
    const int window_span = 1 + kWindowHistoryBars + kWindowAheadBars;
    WindowSlice slice;
    if (composing_)
    {
        const int available = composer_->ensure(program_, first_bar + window_span);
        first_bar = std::clamp(first_bar, 0, std::max(0, available - 1));
        slice.count = std::min(window_span, available - first_bar);
        std::vector<SourceSlicer::StreamBar> items;
        items.reserve(static_cast<size_t>(slice.count));
        for (int slot = first_bar; slot < first_bar + slice.count; ++slot)
        {
            const StreamBarPlan& plan = program_.plan(slot);
            if (!plan.reason.empty() && slot > last_logged_plan_slot_)
                DRAXUL_LOG_INFO(
                    LogCategory::App, "stream: slot %d = %s", slot, plan.reason.c_str());
            last_logged_plan_slot_ = std::max(last_logged_plan_slot_, slot);
            SourceSlicer::StreamBar item;
            if (plan.kind == StreamBarPlan::Kind::Drill)
                item.measure_xml = plan.drill_xml;
            else
                item.source_bar = plan.source_bar;
            items.push_back(std::move(item));
        }
        slice.xml = slicer_.window_xml_for(items, program_.plan(first_bar).source_bar);
        slice.stream_offset_q = program_.slot_start_q(first_bar);
    }
    else
    {
        const int total = slicer_.bar_count();
        first_bar = std::clamp(first_bar, 0, std::max(0, total - 1));
        slice.count = std::min(window_span, total - first_bar);
        slice.xml = slicer_.window_xml(first_bar, slice.count);
        slice.stream_offset_q = slicer_.bar_start_q(first_bar);
    }
    slice.first_bar = first_bar;
    if (slice.xml.empty())
        return std::nullopt;
    return slice;
}

std::optional<int> ScoreStreamController::advance_target(
    double stream_q, int ahead_needed) const
{
    int playhead_bar = 0;
    if (composing_)
    {
        if (composer_->finished() && window_first_bar_ + window_bar_count_ >= program_.size())
            return std::nullopt; // the program is complete and the window reaches its end
        playhead_bar = program_.slot_at(stream_q);
    }
    else
    {
        if (window_first_bar_ + window_bar_count_ >= slicer_.bar_count())
            return std::nullopt; // the window tail already reaches the final bar
        playhead_bar = slicer_.bar_at(stream_q);
    }
    // Advance only when the playhead comes within the viewport's look-ahead
    // of the window's tail — NOT every bar. The background engrave is only
    // ~100 ms while the window still has runway ahead, so the old window
    // keeps playing smoothly until the fresh one installs. It also keeps
    // kWindowHistoryBars behind the playhead so the scroll never clamps.
    const int window_end = window_first_bar_ + window_bar_count_;
    if (playhead_bar <= window_first_bar_ + kWindowHistoryBars
        || playhead_bar < window_end - ahead_needed)
        return std::nullopt;
    return playhead_bar - kWindowHistoryBars;
}

bool ScoreStreamController::queue_engrave(
    WindowSlice&& slice, const EngraveParams& params, PendingInstall intent)
{
    WindowEngraver::Job job;
    job.window_xml = std::move(slice.xml);
    job.params = params;
    job.first_bar = slice.first_bar;
    job.count = slice.count;
    job.stream_offset_q = slice.stream_offset_q;
    const WindowEngraver::RequestId request_id = engraver_->submit(std::move(job));
    if (request_id == 0)
        return false;
    intent.request_id = request_id;
    pending_install_ = intent;
    async_in_flight_ = true;
    DRAXUL_LOG_DEBUG(LogCategory::App, "score: queued window generation %llu for bars %d..%d",
        static_cast<unsigned long long>(request_id), slice.first_bar,
        slice.first_bar + slice.count - 1);
    return true;
}

void ScoreStreamController::cancel_async()
{
    if (engraver_)
        engraver_->cancel();
    pending_install_.reset();
    async_in_flight_ = false;
}

std::optional<ScoreStreamController::Completed> ScoreStreamController::poll_completed()
{
    if (!engraver_)
        return std::nullopt;
    auto done = engraver_->poll();
    if (!done)
        return std::nullopt;
    return filter_completion(std::move(*done));
}

std::optional<ScoreStreamController::Completed> ScoreStreamController::filter_completion(
    WindowEngraver::Done done)
{
    if (!pending_install_)
    {
        DRAXUL_LOG_DEBUG(LogCategory::App,
            "score: host discarded unexpected window generation %llu",
            static_cast<unsigned long long>(done.request_id));
        async_in_flight_ = false;
        return std::nullopt;
    }
    if (done.request_id != pending_install_->request_id)
    {
        // A superseded generation must not retire the newest pending
        // install. WindowEngraver normally filters these itself, but
        // retaining the latest request here keeps the stream robust to
        // delayed/fake workers.
        DRAXUL_LOG_DEBUG(LogCategory::App,
            "score: host discarded stale window generation %llu; waiting for %llu",
            static_cast<unsigned long long>(done.request_id),
            static_cast<unsigned long long>(pending_install_->request_id));
        return std::nullopt;
    }
    Completed completed;
    completed.intent = *pending_install_;
    completed.done = std::move(done);
    pending_install_.reset();
    async_in_flight_ = false;
    return completed;
}

} // namespace scoreview
} // namespace draxul
