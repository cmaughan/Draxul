#pragma once

#include <draxul/ui_panel.h>

#include <string>
#include <vector>

namespace draxul
{

/// Collects startup timing data and other diagnostics that the UI panel displays.
/// Decouples telemetry accumulation from the App orchestrator.
class DiagnosticsCollector
{
public:
    void record_startup_step(std::string name, double elapsed_ms)
    {
        startup_steps_.push_back(StartupStep{ std::move(name), elapsed_ms });
        startup_total_ms_ += elapsed_ms;
    }

    void set_startup_total_ms(double total_ms)
    {
        startup_total_ms_ = total_ms;
    }

    void clear_startup_steps()
    {
        startup_steps_.clear();
        startup_total_ms_ = 0.0;
    }

    void amend_last_step_label(std::string new_label)
    {
        if (!startup_steps_.empty())
            startup_steps_.back().label = std::move(new_label);
    }

    const std::vector<StartupStep>& startup_steps() const
    {
        return startup_steps_;
    }
    double startup_total_ms() const
    {
        return startup_total_ms_;
    }

private:
    std::vector<StartupStep> startup_steps_;
    double startup_total_ms_ = 0.0;
};

} // namespace draxul
