#include "agent_process_observer.h"

#include <algorithm>

namespace draxul::detail
{

AgentProcessObserver::AgentProcessObserver(Probe probe,
    WaitForChange wait_for_change, PollForChange poll_for_change,
    WakeNativeWait wake_native_wait)
    : AgentProcessObserver(std::move(probe), std::move(wait_for_change),
          std::move(poll_for_change), std::move(wake_native_wait), Options{})
{
}

AgentProcessObserver::AgentProcessObserver(Probe probe,
    WaitForChange wait_for_change, PollForChange poll_for_change,
    WakeNativeWait wake_native_wait, Options options)
    : probe_(std::move(probe))
    , wait_for_change_(std::move(wait_for_change))
    , poll_for_change_(std::move(poll_for_change))
    , wake_native_wait_(std::move(wake_native_wait))
    , options_(options)
{
}

AgentProcessObserver::~AgentProcessObserver()
{
    stop();
}

void AgentProcessObserver::start()
{
    std::lock_guard lock(lifecycle_mutex_);
    if (retired_ || thread_.joinable())
        return;
    running_ = true;
    thread_ = std::thread([this] { run(); });
}

void AgentProcessObserver::note_activity()
{
    ++activity_generation_;
    wake_.notify_one();
}

std::optional<AgentProcessObservation> AgentProcessObserver::latest() const
{
    std::lock_guard lock(publication_mutex_);
    return latest_;
}

void AgentProcessObserver::stop()
{
    std::lock_guard lock(lifecycle_mutex_);
    retired_ = true;
    if (!thread_.joinable())
    {
        running_ = false;
        activity_generation_ = 0;
        std::lock_guard publication_lock(publication_mutex_);
        latest_.reset();
        return;
    }
    running_ = false;
    wake_.notify_all();
    if (wake_native_wait_)
        wake_native_wait_();
    if (thread_.joinable())
        thread_.join();
    activity_generation_ = 0;
    std::lock_guard publication_lock(publication_mutex_);
    latest_.reset();
}

bool AgentProcessObserver::wait_for_change(std::chrono::milliseconds timeout)
{
    if (wait_for_change_)
        return wait_for_change_(timeout);
    std::unique_lock lock(wait_mutex_);
    // Output activity is itself a scheduling input. Return on every
    // notification so run() can compare the activity generation immediately;
    // a stop wake is distinguished by running_ after the wait.
    wake_.wait_for(lock, timeout);
    return false;
}

void AgentProcessObserver::run()
{
    AgentObservationSchedule schedule(
        AgentObservationSchedule::Clock::now(), activity_generation_.load(),
        options_.intervals);
    while (running_)
    {
        const auto now = AgentObservationSchedule::Clock::now();
        const auto deadline = schedule.next_deadline();
        const auto remaining = deadline > now
            ? std::chrono::duration_cast<std::chrono::milliseconds>(
                  deadline - now)
            : std::chrono::milliseconds::zero();
        const auto wait = std::clamp(remaining,
            std::chrono::milliseconds::zero(), options_.poll_ceiling);
        bool changed = wait_for_change(wait);
        if (!running_)
            break;
        if (poll_for_change_)
            changed = poll_for_change_() || changed;

        const auto after_wait = AgentObservationSchedule::Clock::now();
        const uint64_t activity = activity_generation_.load();
        if (changed)
            schedule.note_change(after_wait, activity);
        else
            schedule.note_activity(after_wait, activity);
        if (!schedule.due(after_wait))
            continue;

        auto observation = probe_ ? probe_() : std::nullopt;
        const bool agent_present = observation
            && discover_agent_process(*observation).has_value();
        {
            std::lock_guard lock(publication_mutex_);
            latest_ = std::move(observation);
        }
        schedule.complete(after_wait, agent_present, activity);
    }
}

} // namespace draxul::detail
