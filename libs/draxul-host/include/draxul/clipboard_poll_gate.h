#pragma once

#include <chrono>
#include <optional>

namespace draxul
{

class ClipboardPollGate
{
public:
    using Clock = std::chrono::steady_clock;

    explicit ClipboardPollGate(Clock::duration interval = std::chrono::milliseconds(500))
        : interval_(interval)
    {
    }

    bool should_poll(Clock::time_point now)
    {
        if (!last_poll_ || now - *last_poll_ >= interval_)
        {
            last_poll_ = now;
            return true;
        }
        return false;
    }

    void mark_polled(Clock::time_point now)
    {
        last_poll_ = now;
    }

private:
    Clock::duration interval_;
    std::optional<Clock::time_point> last_poll_;
};

} // namespace draxul
