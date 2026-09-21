#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>

namespace draxul::detail
{

// Pure scheduling state for platform agent-process observers. Native probes,
// borrowed process/PTY handles, threads, and publication remain transport
// owned; this value makes debounce and reconciliation deterministic to test.
class AgentObservationSchedule
{
public:
    using Clock = std::chrono::steady_clock;
    struct Intervals
    {
        Clock::duration change_debounce = std::chrono::seconds(1);
        Clock::duration present_reconcile = std::chrono::seconds(5);
        Clock::duration missing_reconcile = std::chrono::seconds(30);
    };

    AgentObservationSchedule(Clock::time_point now, uint64_t activity)
        : AgentObservationSchedule(now, activity, Intervals{})
    {
    }

    AgentObservationSchedule(Clock::time_point now, uint64_t activity,
        Intervals intervals)
        : intervals_(intervals)
        , observed_activity_(activity)
        , refresh_at_(now)
        , reconcile_at_(now)
    {
    }

    void note_change(Clock::time_point now, uint64_t activity,
        bool immediate = false)
    {
        if (immediate)
            refresh_at_ = now;
        else if (!dirty_)
            refresh_at_ = now + intervals_.change_debounce;
        dirty_ = true;
        observed_activity_ = activity;
    }

    void note_activity(Clock::time_point now, uint64_t activity)
    {
        if (!agent_present_ && activity != observed_activity_)
            note_change(now, activity);
    }

    [[nodiscard]] bool due(Clock::time_point now) const
    {
        return (dirty_ && now >= refresh_at_) || now >= reconcile_at_;
    }

    [[nodiscard]] Clock::time_point next_deadline() const
    {
        return dirty_ ? std::min(refresh_at_, reconcile_at_) : reconcile_at_;
    }

    void complete(Clock::time_point now, bool agent_present,
        uint64_t activity)
    {
        dirty_ = false;
        agent_present_ = agent_present;
        observed_activity_ = activity;
        reconcile_at_ = now + (agent_present ? intervals_.present_reconcile : intervals_.missing_reconcile);
    }

    [[nodiscard]] bool agent_present() const
    {
        return agent_present_;
    }

private:
    Intervals intervals_;
    uint64_t observed_activity_ = 0;
    Clock::time_point refresh_at_;
    Clock::time_point reconcile_at_;
    bool dirty_ = true;
    bool agent_present_ = false;
};

} // namespace draxul::detail
