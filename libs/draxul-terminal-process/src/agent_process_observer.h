#pragma once

#include "agent_observation_schedule.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <draxul/agent_model.h>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>

namespace draxul::detail
{

// Threaded publication owner for agent-process observations. Platform adapters
// provide probes and optional native change waits; this class owns scheduling,
// activity generations, caching, lazy thread startup, and bounded stop/join.
class AgentProcessObserver
{
public:
    using Probe = std::function<std::optional<AgentProcessObservation>()>;
    using WaitForChange = std::function<bool(std::chrono::milliseconds)>;
    using PollForChange = std::function<bool()>;
    using WakeNativeWait = std::function<void()>;

    struct Options
    {
        AgentObservationSchedule::Intervals intervals;
        std::chrono::milliseconds poll_ceiling = std::chrono::seconds(1);
    };

    explicit AgentProcessObserver(Probe probe,
        WaitForChange wait_for_change = {},
        PollForChange poll_for_change = {},
        WakeNativeWait wake_native_wait = {});
    AgentProcessObserver(Probe probe, WaitForChange wait_for_change,
        PollForChange poll_for_change, WakeNativeWait wake_native_wait,
        Options options);
    ~AgentProcessObserver();

    AgentProcessObserver(const AgentProcessObserver&) = delete;
    AgentProcessObserver& operator=(const AgentProcessObserver&) = delete;

    void start();
    void note_activity();
    [[nodiscard]] std::optional<AgentProcessObservation> latest() const;
    void stop();

private:
    void run();
    bool wait_for_change(std::chrono::milliseconds timeout);

    Probe probe_;
    WaitForChange wait_for_change_;
    PollForChange poll_for_change_;
    WakeNativeWait wake_native_wait_;
    Options options_;

    mutable std::mutex lifecycle_mutex_;
    mutable std::mutex publication_mutex_;
    std::mutex wait_mutex_;
    std::condition_variable wake_;
    std::thread thread_;
    std::atomic<bool> running_{ false };
    std::atomic<uint64_t> activity_generation_{ 0 };
    bool retired_ = false;
    std::optional<AgentProcessObservation> latest_;
};

} // namespace draxul::detail
