#include <draxul/cursor_blinker.h>
#include <draxul/perf_timing.h>

namespace draxul
{

void CursorBlinker::restart(std::chrono::steady_clock::time_point now, bool busy, const BlinkTiming& timing)
{
    PERF_MEASURE();
    timing_ = timing;

    if (busy)
    {
        visible_ = false;
        phase_ = Phase::Steady;
        next_deadline_.reset();
        return;
    }

    visible_ = true;
    if (!timing.enabled())
    {
        phase_ = Phase::Steady;
        next_deadline_.reset();
    }
    else
    {
        phase_ = Phase::Wait;
        next_deadline_ = now + std::chrono::milliseconds(timing.blinkwait);
    }
}

bool CursorBlinker::advance(std::chrono::steady_clock::time_point now)
{
    PERF_MEASURE();
    if (!next_deadline_ || now < *next_deadline_)
        return false;

    if (!timing_.enabled())
    {
        // Blink was disabled while a cycle was running — reset to steady-visible.
        bool changed = !visible_;
        visible_ = true;
        phase_ = Phase::Steady;
        next_deadline_.reset();
        return changed;
    }

    switch (phase_)
    {
    case Phase::Wait:
    case Phase::On:
        visible_ = false;
        phase_ = Phase::Off;
        next_deadline_ = now + std::chrono::milliseconds(timing_.blinkoff);
        return true;

    case Phase::Off:
        visible_ = true;
        phase_ = Phase::On;
        next_deadline_ = now + std::chrono::milliseconds(timing_.blinkon);
        return true;

    case Phase::Steady:
        next_deadline_.reset();
        return false;
    }

    return false;
}

} // namespace draxul
