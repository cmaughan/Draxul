#pragma once

#include <atomic>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <draxul/satview/satview_catalog.h>
#include <draxul/satview/satview_config.h>
#include <draxul/satview/satview_propagation.h>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace draxul::satview
{

struct SatViewSimulationControls
{
    float time_speed = 60.0f;
    bool paused = false;
    std::size_t track_satellite_limit = kDefaultTrackSatelliteLimit;
    std::size_t track_sample_count = kDefaultTrackSampleCount;
    bool refresh_tracks_each_step = false;
    std::optional<std::int64_t> selected_track_norad_catalog_id;
    std::optional<CentralBody> track_central_body;
};

struct SatViewSimulationSnapshot
{
    std::uint64_t generation = 0;
    std::uint64_t catalog_generation = 0;
    double simulation_seconds = 0.0;
    double next_simulation_seconds = 0.0;
    std::chrono::steady_clock::time_point produced_at{};
    float time_speed = 60.0f;
    bool paused = false;
    std::string source_label;
    std::string source_url;
    std::string status_text;
    std::vector<SatellitePropagatedState> states;
    std::vector<glm::dvec3> next_teme_positions_km;
    std::shared_ptr<const std::vector<SatelliteOrbitTrack>> tracks;
    std::size_t propagation_concurrency = 1;
    std::size_t skipped_model_records = 0;
    std::size_t failed_propagations = 0;
    std::string error;
};

[[nodiscard]] double satview_snapshot_render_seconds(
    const SatViewSimulationSnapshot& snapshot,
    std::chrono::steady_clock::time_point now);
[[nodiscard]] bool satview_selected_track_needs_refresh(
    const SatelliteOrbitTrack& track,
    double simulation_seconds);

class SatViewSnapshotExchange
{
private:
    struct Slot
    {
        mutable std::atomic<std::uint32_t> readers{ 0 };
        SatViewSimulationSnapshot snapshot;
    };

public:
    class ReadGuard
    {
    public:
        ReadGuard() = default;
        ~ReadGuard();

        ReadGuard(const ReadGuard&) = delete;
        ReadGuard& operator=(const ReadGuard&) = delete;

        ReadGuard(ReadGuard&& other) noexcept;
        ReadGuard& operator=(ReadGuard&& other) noexcept;

        [[nodiscard]] explicit operator bool() const { return snapshot_ != nullptr; }
        [[nodiscard]] const SatViewSimulationSnapshot* get() const { return snapshot_; }
        [[nodiscard]] const SatViewSimulationSnapshot& operator*() const { return *snapshot_; }
        [[nodiscard]] const SatViewSimulationSnapshot* operator->() const { return snapshot_; }

    private:
        ReadGuard(const Slot* slot, const SatViewSimulationSnapshot* snapshot);
        void release();

        const Slot* slot_ = nullptr;
        const SatViewSimulationSnapshot* snapshot_ = nullptr;

        friend class SatViewSnapshotExchange;
    };

    [[nodiscard]] ReadGuard acquire_latest() const;
    bool publish(SatViewSimulationSnapshot snapshot);
    [[nodiscard]] std::uint64_t latest_generation() const;

private:
    std::array<Slot, 3> slots_;
    std::atomic<int> published_slot_{ -1 };
    std::atomic<std::uint64_t> latest_generation_{ 0 };
};

class SatViewSimulationWorker
{
public:
    SatViewSimulationWorker() = default;
    ~SatViewSimulationWorker();

    SatViewSimulationWorker(const SatViewSimulationWorker&) = delete;
    SatViewSimulationWorker& operator=(const SatViewSimulationWorker&) = delete;

    void start(double initial_simulation_seconds, const SatViewSimulationControls& controls);
    void stop();

    void set_catalog(SatelliteCatalog catalog, std::uint64_t catalog_generation);
    void set_controls(float time_speed, bool paused);
    void set_clock(double simulation_seconds, float time_speed, bool paused);
    void set_render_settings(
        std::size_t track_satellite_limit,
        std::size_t track_sample_count,
        bool refresh_tracks_each_step,
        std::optional<std::int64_t> selected_track_norad_catalog_id,
        std::optional<CentralBody> track_central_body);

    [[nodiscard]] SatViewSnapshotExchange::ReadGuard acquire_latest() const;
    [[nodiscard]] double current_simulation_seconds() const;

private:
    using Clock = std::chrono::steady_clock;

    struct ClockState
    {
        double base_simulation_seconds = 0.0;
        Clock::time_point base_time = Clock::now();
        float time_speed = 60.0f;
        bool paused = false;
    };

    void run_loop();
    [[nodiscard]] double current_simulation_seconds_locked(Clock::time_point now) const;
    void publish_empty_snapshot(
        std::uint64_t& generation,
        std::uint64_t catalog_generation,
        double simulation_seconds,
        std::string status_text,
        std::string error = {});

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    bool running_ = false;
    bool stop_requested_ = false;
    bool catalog_dirty_ = false;
    bool clock_dirty_ = false;
    bool settings_dirty_ = false;
    SatelliteCatalog pending_catalog_;
    std::uint64_t pending_catalog_generation_ = 0;
    ClockState clock_state_;
    SatViewSimulationControls controls_;

    mutable SatViewSnapshotExchange snapshots_;
    std::thread worker_;
};

} // namespace draxul::satview
