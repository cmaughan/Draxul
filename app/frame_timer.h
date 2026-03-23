#pragma once
#include <array>
#include <cstddef>

namespace draxul
{

struct FrameTimer
{
    void record(double ms)
    {
        last_ = ms;
        samples_[index_] = ms;
        index_ = (index_ + 1) % samples_.size();
        if (count_ < samples_.size())
            ++count_;
    }

    double last_ms() const
    {
        return last_;
    }

    double average_ms() const
    {
        if (count_ == 0)
            return 0.0;
        double total = 0.0;
        for (size_t i = 0; i < count_; ++i)
            total += samples_[i];
        return total / static_cast<double>(count_);
    }

private:
    std::array<double, 32> samples_{};
    size_t count_ = 0;
    size_t index_ = 0;
    double last_ = 0.0;
};

} // namespace draxul
