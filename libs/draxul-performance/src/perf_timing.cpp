#include <draxul/perf_timing.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <mutex>
#include <string_view>
#include <unordered_map>

namespace draxul
{

namespace
{

using Clock = std::chrono::steady_clock;

constexpr float kSmoothingAlpha = 1.0f / 100.0f;
constexpr uint32_t kStaleFramePruneThreshold = 300;
constexpr float kMinPrunableSmoothedFraction = 1.0e-5f;

struct ResolvedPerfTimingTag
{
    std::string source_file_path;
    std::string owner_qualified_name;
    std::string function_name;
    std::string pretty_function;
};

struct FrameMetric
{
    uint64_t microseconds = 0;
    uint32_t call_count = 0;
};

struct SmoothedMetric
{
    ResolvedPerfTimingTag resolved;
    uint64_t frame_microseconds = 0;
    uint64_t smoothed_microseconds = 0;
    float frame_fraction = 0.0f;
    float smoothed_frame_fraction = 0.0f;
    float normalized_heat = 0.0f;
    uint32_t call_count = 0;
    uint32_t stale_frames = 0;
};

class RuntimePerfCollectorImpl
{
public:
    void set_enabled(bool enabled)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        enabled_.store(enabled, std::memory_order_release);
        if (!enabled)
            frame_active_ = false;
    }

    [[nodiscard]] bool enabled() const
    {
        return enabled_.load(std::memory_order_acquire);
    }

    void begin_frame()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!enabled_.load(std::memory_order_relaxed))
            return;

        frame_active_ = true;
        frame_start_ = Clock::now();
        frame_metrics_.clear();
    }

    void cancel_frame()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        frame_active_ = false;
        frame_metrics_.clear();
    }

    void end_frame(uint64_t frame_time_override_microseconds)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!enabled_.load(std::memory_order_relaxed) || !frame_active_)
            return;

        frame_active_ = false;
        ++frame_index_;

        uint64_t frame_time_microseconds = frame_time_override_microseconds;
        if (frame_time_microseconds == 0)
        {
            frame_time_microseconds = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                Clock::now() - frame_start_)
                    .count());
        }
        frame_time_microseconds = std::max<uint64_t>(frame_time_microseconds, 1u);

        for (auto& [tag, metric] : smoothed_metrics_)
        {
            const auto frame_it = frame_metrics_.find(tag);
            const uint64_t frame_microseconds = frame_it != frame_metrics_.end() ? frame_it->second.microseconds : 0u;
            const uint32_t call_count = frame_it != frame_metrics_.end() ? frame_it->second.call_count : 0u;
            const float frame_fraction
                = static_cast<float>(static_cast<double>(frame_microseconds) / static_cast<double>(frame_time_microseconds));

            metric.frame_microseconds = frame_microseconds;
            metric.frame_fraction = std::clamp(frame_fraction, 0.0f, 1.0f);
            metric.smoothed_frame_fraction += kSmoothingAlpha * (metric.frame_fraction - metric.smoothed_frame_fraction);
            metric.smoothed_frame_fraction = std::max(metric.smoothed_frame_fraction, 0.0f);
            metric.smoothed_microseconds = static_cast<uint64_t>(std::llround(
                static_cast<double>(metric.smoothed_frame_fraction) * static_cast<double>(frame_time_microseconds)));
            metric.call_count = call_count;
            metric.stale_frames = frame_it != frame_metrics_.end() ? 0u : (metric.stale_frames + 1u);
        }

        float max_smoothed_fraction = 0.0f;
        for (const auto& [_, metric] : smoothed_metrics_)
            max_smoothed_fraction = std::max(max_smoothed_fraction, metric.smoothed_frame_fraction);
        const float inv_max_smoothed_fraction = max_smoothed_fraction > 1.0e-6f ? 1.0f / max_smoothed_fraction : 0.0f;

        latest_snapshot_.generation = ++generation_;
        latest_snapshot_.frame_index = frame_index_;
        latest_snapshot_.frame_time_microseconds = frame_time_microseconds;
        latest_snapshot_.functions.clear();
        latest_snapshot_.functions.reserve(smoothed_metrics_.size());

        for (auto it = smoothed_metrics_.begin(); it != smoothed_metrics_.end();)
        {
            it->second.normalized_heat
                = std::clamp(it->second.smoothed_frame_fraction * inv_max_smoothed_fraction, 0.0f, 1.0f);
            if (it->second.stale_frames >= kStaleFramePruneThreshold
                && it->second.smoothed_frame_fraction <= kMinPrunableSmoothedFraction)
            {
                it = smoothed_metrics_.erase(it);
                continue;
            }

            latest_snapshot_.functions.push_back({
                .source_file_path = it->second.resolved.source_file_path,
                .owner_qualified_name = it->second.resolved.owner_qualified_name,
                .function_name = it->second.resolved.function_name,
                .pretty_function = it->second.resolved.pretty_function,
                .frame_microseconds = it->second.frame_microseconds,
                .smoothed_microseconds = it->second.smoothed_microseconds,
                .frame_fraction = it->second.frame_fraction,
                .smoothed_frame_fraction = it->second.smoothed_frame_fraction,
                .normalized_heat = it->second.normalized_heat,
                .call_count = it->second.call_count,
            });
            ++it;
        }

        std::sort(
            latest_snapshot_.functions.begin(),
            latest_snapshot_.functions.end(),
            [](const RuntimePerfFunctionTiming& lhs, const RuntimePerfFunctionTiming& rhs) {
                if (lhs.normalized_heat != rhs.normalized_heat)
                    return lhs.normalized_heat > rhs.normalized_heat;
                if (lhs.smoothed_frame_fraction != rhs.smoothed_frame_fraction)
                    return lhs.smoothed_frame_fraction > rhs.smoothed_frame_fraction;
                if (lhs.source_file_path != rhs.source_file_path)
                    return lhs.source_file_path < rhs.source_file_path;
                if (lhs.owner_qualified_name != rhs.owner_qualified_name)
                    return lhs.owner_qualified_name < rhs.owner_qualified_name;
                return lhs.function_name < rhs.function_name;
            });

        frame_metrics_.clear();
    }

    void report_timing(const PerfTimingTag& tag, uint64_t microseconds)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!enabled_.load(std::memory_order_relaxed) || !frame_active_ || microseconds == 0)
            return;

        FrameMetric& frame_metric = frame_metrics_[&tag];
        frame_metric.microseconds += microseconds;
        ++frame_metric.call_count;

        auto smoothed_it = smoothed_metrics_.find(&tag);
        if (smoothed_it == smoothed_metrics_.end())
        {
            SmoothedMetric smoothed;
            smoothed.resolved = resolve_tag(tag);
            smoothed_it = smoothed_metrics_.emplace(&tag, std::move(smoothed)).first;
        }
        smoothed_it->second.stale_frames = 0;
    }

    [[nodiscard]] RuntimePerfSnapshot latest_snapshot() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return latest_snapshot_;
    }

    void reset()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        frame_active_ = false;
        frame_metrics_.clear();
        smoothed_metrics_.clear();
        latest_snapshot_ = RuntimePerfSnapshot{};
        generation_ = 0;
        frame_index_ = 0;
    }

private:
    [[nodiscard]] static std::string normalize_source_path(std::string_view raw_path)
    {
        std::string normalized(raw_path);
        std::replace(normalized.begin(), normalized.end(), '\\', '/');
        while (normalized.rfind("./", 0) == 0)
            normalized.erase(0, 2);

        static constexpr std::array<std::string_view, 5> kRootPrefixes = {
            "app/",
            "libs/",
            "tests/",
            "shaders/",
            "docs/",
        };

        for (std::string_view prefix : kRootPrefixes)
        {
            if (normalized.rfind(prefix, 0) == 0)
                return normalized;

            const std::string needle = "/" + std::string(prefix);
            const size_t pos = normalized.find(needle);
            if (pos != std::string::npos)
                return normalized.substr(pos + 1);
        }

        const size_t repo_pos = normalized.find("Draxul/");
        if (repo_pos != std::string::npos)
            return normalized.substr(repo_pos + std::string_view("Draxul/").size());

        return normalized;
    }

    [[nodiscard]] static std::string trim_spaces(std::string value)
    {
        const auto is_space = [](unsigned char ch) { return std::isspace(ch) != 0; };
        value.erase(value.begin(), std::find_if(value.begin(), value.end(), [&](char ch) { return !is_space(ch); }));
        value.erase(
            std::find_if(value.rbegin(), value.rend(), [&](char ch) { return !is_space(ch); }).base(),
            value.end());
        return value;
    }

    [[nodiscard]] static std::string short_qualified_name(std::string_view qualified_name)
    {
        const size_t last_scope = qualified_name.rfind("::");
        if (last_scope == std::string_view::npos)
            return std::string(qualified_name);
        return std::string(qualified_name.substr(last_scope + 2));
    }

    [[nodiscard]] static ResolvedPerfTimingTag resolve_tag(const PerfTimingTag& tag)
    {
        ResolvedPerfTimingTag resolved;
        resolved.source_file_path = normalize_source_path(tag.file ? tag.file : "");
        resolved.pretty_function = tag.pretty_function ? tag.pretty_function : "";
        resolved.function_name = tag.function ? tag.function : "";

        std::string signature = resolved.pretty_function;
        const size_t paren = signature.find('(');
        if (paren != std::string::npos)
            signature.resize(paren);

        if (const size_t draxul_namespace = signature.find("draxul::"); draxul_namespace != std::string::npos)
            signature.erase(0, draxul_namespace + std::string_view("draxul::").size());

        signature = trim_spaces(signature);
        if (const size_t last_space = signature.rfind(' '); last_space != std::string::npos)
        {
            const std::string trailing = signature.substr(last_space + 1);
            if (trailing.find("::") != std::string::npos)
                signature = trailing;
        }

        const size_t last_scope = signature.rfind("::");
        if (last_scope != std::string::npos)
        {
            resolved.owner_qualified_name = signature.substr(0, last_scope);
            resolved.function_name = signature.substr(last_scope + 2);
            if (resolved.owner_qualified_name == resolved.function_name)
                resolved.owner_qualified_name = short_qualified_name(resolved.owner_qualified_name);
        }

        return resolved;
    }

    mutable std::mutex mutex_;
    std::atomic_bool enabled_{false};
    bool frame_active_ = false;
    Clock::time_point frame_start_{};
    uint64_t generation_ = 0;
    uint64_t frame_index_ = 0;
    std::unordered_map<const PerfTimingTag*, FrameMetric> frame_metrics_;
    std::unordered_map<const PerfTimingTag*, SmoothedMetric> smoothed_metrics_;
    RuntimePerfSnapshot latest_snapshot_;
};

RuntimePerfCollectorImpl& collector_impl()
{
    static RuntimePerfCollectorImpl collector;
    return collector;
}

uint64_t now_microseconds()
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(Clock::now().time_since_epoch()).count());
}

} // namespace

void RuntimePerfCollector::set_enabled(bool enabled)
{
    collector_impl().set_enabled(enabled);
}

bool RuntimePerfCollector::enabled() const
{
    return collector_impl().enabled();
}

void RuntimePerfCollector::begin_frame()
{
    collector_impl().begin_frame();
}

void RuntimePerfCollector::cancel_frame()
{
    collector_impl().cancel_frame();
}

void RuntimePerfCollector::end_frame(uint64_t frame_time_override_microseconds)
{
    collector_impl().end_frame(frame_time_override_microseconds);
}

void RuntimePerfCollector::report_timing(const PerfTimingTag& tag, uint64_t microseconds)
{
    collector_impl().report_timing(tag, microseconds);
}

RuntimePerfSnapshot RuntimePerfCollector::latest_snapshot() const
{
    return collector_impl().latest_snapshot();
}

void RuntimePerfCollector::reset()
{
    collector_impl().reset();
}

RuntimePerfCollector& runtime_perf_collector()
{
    static RuntimePerfCollector collector;
    return collector;
}

ScopedPerfMeasure::ScopedPerfMeasure(const PerfTimingTag& tag)
    : tag_(&tag)
    , active_(runtime_perf_collector().enabled())
{
    if (active_)
        start_microseconds_ = now_microseconds();
}

ScopedPerfMeasure::~ScopedPerfMeasure()
{
    if (!active_ || !tag_)
        return;
    const uint64_t end_microseconds = now_microseconds();
    if (end_microseconds > start_microseconds_)
        runtime_perf_collector().report_timing(*tag_, end_microseconds - start_microseconds_);
}

} // namespace draxul
