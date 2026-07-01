#include "satview_simulation_worker.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iterator>
#include <utility>

namespace draxul::satview
{

namespace
{

constexpr auto kPositionTick = std::chrono::milliseconds(16);

std::string make_status_text(const SatViewSimulationSnapshot& snapshot)
{
    if (!snapshot.error.empty())
        return "orbit " + snapshot.error;

    std::string text = "orbit " + std::to_string(snapshot.states.size()) + " positions";
    if (snapshot.tracks && !snapshot.tracks->empty())
        text += ", " + std::to_string(snapshot.tracks->size()) + " tracks";
    text += ", " + std::to_string(snapshot.propagation_concurrency) + " threads";
    if (snapshot.failed_propagations != 0)
        text += ", " + std::to_string(snapshot.failed_propagations) + " failed";
    return text;
}

} // namespace

double satview_snapshot_render_seconds(
    const SatViewSimulationSnapshot& snapshot,
    std::chrono::steady_clock::time_point now)
{
    if (snapshot.paused || snapshot.next_simulation_seconds <= snapshot.simulation_seconds)
        return snapshot.simulation_seconds;

    const auto elapsed = std::chrono::duration<double>(now - snapshot.produced_at).count();
    const double predicted = snapshot.simulation_seconds
        + std::max(0.0, elapsed) * static_cast<double>(snapshot.time_speed);
    return std::clamp(
        predicted,
        snapshot.simulation_seconds,
        snapshot.next_simulation_seconds);
}

bool satview_selected_track_needs_refresh(
    const SatelliteOrbitTrack& track,
    double simulation_seconds)
{
    constexpr double kRefreshWindowFraction = 0.25;
    if (!std::isfinite(simulation_seconds)
        || !std::isfinite(track.sample_center_unix_seconds)
        || track.sample_horizon_minutes <= 0.0)
    {
        return false;
    }
    const double refresh_interval_seconds = track.sample_horizon_minutes
        * 60.0 * kRefreshWindowFraction;
    return std::abs(simulation_seconds - track.sample_center_unix_seconds)
        >= refresh_interval_seconds;
}

SatViewSnapshotExchange::ReadGuard::ReadGuard(const Slot* slot, const SatViewSimulationSnapshot* snapshot)
    : slot_(slot)
    , snapshot_(snapshot)
{
}

SatViewSnapshotExchange::ReadGuard::~ReadGuard()
{
    release();
}

SatViewSnapshotExchange::ReadGuard::ReadGuard(ReadGuard&& other) noexcept
    : slot_(other.slot_)
    , snapshot_(other.snapshot_)
{
    other.slot_ = nullptr;
    other.snapshot_ = nullptr;
}

SatViewSnapshotExchange::ReadGuard& SatViewSnapshotExchange::ReadGuard::operator=(ReadGuard&& other) noexcept
{
    if (this == &other)
        return *this;
    release();
    slot_ = other.slot_;
    snapshot_ = other.snapshot_;
    other.slot_ = nullptr;
    other.snapshot_ = nullptr;
    return *this;
}

void SatViewSnapshotExchange::ReadGuard::release()
{
    if (slot_)
        slot_->readers.fetch_sub(1, std::memory_order_release);
    slot_ = nullptr;
    snapshot_ = nullptr;
}

SatViewSnapshotExchange::ReadGuard SatViewSnapshotExchange::acquire_latest() const
{
    for (;;)
    {
        const int index = published_slot_.load(std::memory_order_acquire);
        if (index < 0 || index >= static_cast<int>(slots_.size()))
            return {};

        const auto& slot = slots_[static_cast<std::size_t>(index)];
        slot.readers.fetch_add(1, std::memory_order_acquire);
        if (published_slot_.load(std::memory_order_acquire) == index)
            return ReadGuard(&slot, &slot.snapshot);
        slot.readers.fetch_sub(1, std::memory_order_release);
    }
}

bool SatViewSnapshotExchange::publish(SatViewSimulationSnapshot snapshot)
{
    const int current = published_slot_.load(std::memory_order_acquire);
    for (std::size_t i = 0; i < slots_.size(); ++i)
    {
        if (static_cast<int>(i) == current)
            continue;
        auto& slot = slots_[i];
        if (slot.readers.load(std::memory_order_acquire) != 0)
            continue;

        slot.snapshot = std::move(snapshot);
        latest_generation_.store(slot.snapshot.generation, std::memory_order_release);
        published_slot_.store(static_cast<int>(i), std::memory_order_release);
        return true;
    }
    return false;
}

std::uint64_t SatViewSnapshotExchange::latest_generation() const
{
    return latest_generation_.load(std::memory_order_acquire);
}

SatViewSimulationWorker::~SatViewSimulationWorker()
{
    stop();
}

void SatViewSimulationWorker::start(double initial_simulation_seconds, const SatViewSimulationControls& controls)
{
    stop();
    {
        std::lock_guard lock(mutex_);
        stop_requested_ = false;
        running_ = true;
        controls_ = controls;
        clock_state_.base_simulation_seconds = initial_simulation_seconds;
        clock_state_.base_time = Clock::now();
        clock_state_.time_speed = controls.time_speed;
        clock_state_.paused = controls.paused;
        clock_dirty_ = true;
        settings_dirty_ = true;
    }
    worker_ = std::thread([this]() { run_loop(); });
}

void SatViewSimulationWorker::stop()
{
    {
        std::lock_guard lock(mutex_);
        if (!running_ && !worker_.joinable())
            return;
        stop_requested_ = true;
        running_ = false;
    }
    cv_.notify_all();
    if (worker_.joinable())
        worker_.join();
}

void SatViewSimulationWorker::set_catalog(SatelliteCatalog catalog, std::uint64_t catalog_generation)
{
    {
        std::lock_guard lock(mutex_);
        pending_catalog_ = std::move(catalog);
        pending_catalog_generation_ = catalog_generation;
        catalog_dirty_ = true;
    }
    cv_.notify_all();
}

void SatViewSimulationWorker::set_controls(float time_speed, bool paused)
{
    {
        std::lock_guard lock(mutex_);
        const auto now = Clock::now();
        clock_state_.base_simulation_seconds = current_simulation_seconds_locked(now);
        clock_state_.base_time = now;
        clock_state_.time_speed = std::max(0.0f, time_speed);
        clock_state_.paused = paused;
        controls_.time_speed = std::max(0.0f, time_speed);
        controls_.paused = paused;
        clock_dirty_ = true;
    }
    cv_.notify_all();
}

void SatViewSimulationWorker::set_clock(double simulation_seconds, float time_speed, bool paused)
{
    {
        std::lock_guard lock(mutex_);
        const float clamped_speed = std::max(0.0f, time_speed);
        clock_state_.base_simulation_seconds = simulation_seconds;
        clock_state_.base_time = Clock::now();
        clock_state_.time_speed = clamped_speed;
        clock_state_.paused = paused;
        controls_.time_speed = clamped_speed;
        controls_.paused = paused;
        clock_dirty_ = true;
    }
    cv_.notify_all();
}

void SatViewSimulationWorker::set_render_settings(
    std::size_t track_satellite_limit,
    std::size_t track_sample_count,
    bool refresh_tracks_each_step,
    std::optional<std::int64_t> selected_track_norad_catalog_id)
{
    {
        std::lock_guard lock(mutex_);
        if (controls_.track_satellite_limit == track_satellite_limit
            && controls_.track_sample_count == track_sample_count
            && controls_.refresh_tracks_each_step == refresh_tracks_each_step
            && controls_.selected_track_norad_catalog_id == selected_track_norad_catalog_id)
        {
            return;
        }
        controls_.track_satellite_limit = track_satellite_limit;
        controls_.track_sample_count = track_sample_count;
        controls_.refresh_tracks_each_step = refresh_tracks_each_step;
        controls_.selected_track_norad_catalog_id = selected_track_norad_catalog_id;
        settings_dirty_ = true;
    }
    cv_.notify_all();
}

SatViewSnapshotExchange::ReadGuard SatViewSimulationWorker::acquire_latest() const
{
    return snapshots_.acquire_latest();
}

double SatViewSimulationWorker::current_simulation_seconds() const
{
    std::lock_guard lock(mutex_);
    return current_simulation_seconds_locked(Clock::now());
}

double SatViewSimulationWorker::current_simulation_seconds_locked(Clock::time_point now) const
{
    if (clock_state_.paused)
        return clock_state_.base_simulation_seconds;
    const auto elapsed = std::chrono::duration<double>(now - clock_state_.base_time).count();
    return clock_state_.base_simulation_seconds + elapsed * static_cast<double>(clock_state_.time_speed);
}

void SatViewSimulationWorker::publish_empty_snapshot(
    std::uint64_t& generation,
    std::uint64_t catalog_generation,
    double simulation_seconds,
    std::string status_text,
    std::string error)
{
    SatViewSimulationSnapshot snapshot;
    snapshot.generation = ++generation;
    snapshot.catalog_generation = catalog_generation;
    snapshot.simulation_seconds = simulation_seconds;
    snapshot.next_simulation_seconds = simulation_seconds;
    snapshot.produced_at = Clock::now();
    snapshot.status_text = std::move(status_text);
    snapshot.error = std::move(error);
    snapshots_.publish(std::move(snapshot));
}

void SatViewSimulationWorker::run_loop()
{
    SatellitePropagationExecutor propagation_executor;
    SatellitePropagationModel model;
    std::uint64_t model_catalog_generation = 0;
    std::uint64_t snapshot_generation = 0;
    SatViewSimulationControls controls;
    std::shared_ptr<const std::vector<SatelliteOrbitTrack>> cached_tracks =
        std::make_shared<const std::vector<SatelliteOrbitTrack>>();
    auto next_position_time = Clock::now();
    bool force_tracks = true;

    for (;;)
    {
        SatelliteCatalog catalog;
        std::uint64_t catalog_generation = 0;
        bool have_catalog = false;
        bool settings_changed = false;
        double simulation_seconds = 0.0;

        {
            std::unique_lock lock(mutex_);
            cv_.wait_until(lock, next_position_time, [this]() {
                return stop_requested_ || catalog_dirty_ || clock_dirty_ || settings_dirty_;
            });
            if (stop_requested_)
                break;

            const auto now = Clock::now();
            if (catalog_dirty_)
            {
                catalog = std::move(pending_catalog_);
                catalog_generation = pending_catalog_generation_;
                pending_catalog_ = {};
                catalog_dirty_ = false;
                have_catalog = true;
            }
            if (settings_dirty_)
            {
                settings_dirty_ = false;
                settings_changed = true;
            }
            clock_dirty_ = false;
            controls = controls_;
            simulation_seconds = current_simulation_seconds_locked(now);
        }

        const auto now = Clock::now();
        auto position_sample_time = now;
        if (have_catalog)
        {
            auto build = build_satellite_propagation_model(catalog);
            if (!build)
            {
                model = {};
                model_catalog_generation = catalog_generation;
                cached_tracks = std::make_shared<const std::vector<SatelliteOrbitTrack>>();
                publish_empty_snapshot(
                    snapshot_generation,
                    model_catalog_generation,
                    simulation_seconds,
                    "orbit propagation unavailable",
                    build.error.empty() ? "unavailable" : build.error);
                next_position_time = now + kPositionTick;
                continue;
            }

            model = std::move(build.model);
            model_catalog_generation = catalog_generation;
            cached_tracks = std::make_shared<const std::vector<SatelliteOrbitTrack>>();
            force_tracks = true;
        }

        if (model.empty())
        {
            publish_empty_snapshot(snapshot_generation, model_catalog_generation, simulation_seconds, "orbit awaiting catalog");
            next_position_time = now + kPositionTick;
            continue;
        }

        const bool rebuild_tracks = force_tracks || settings_changed || controls.refresh_tracks_each_step;

        SatellitePropagationSettings propagation_settings;
        propagation_settings.track_sample_count = rebuild_tracks ? controls.track_sample_count : 0;
        propagation_settings.track_satellite_limit = controls.track_satellite_limit;
        propagation_settings.selected_track_norad_catalog_id = controls.selected_track_norad_catalog_id;

        SatellitePropagationResult result = propagate_satellites(
            model,
            simulation_seconds,
            propagation_settings,
            &propagation_executor);
        if (result && rebuild_tracks)
        {
            cached_tracks = std::make_shared<const std::vector<SatelliteOrbitTrack>>(std::move(result.tracks));
            force_tracks = false;

            if (!controls.refresh_tracks_each_step)
            {
                {
                    std::lock_guard lock(mutex_);
                    position_sample_time = Clock::now();
                    controls.time_speed = controls_.time_speed;
                    controls.paused = controls_.paused;
                    simulation_seconds = current_simulation_seconds_locked(position_sample_time);
                }

                SatellitePropagationSettings position_settings;
                result = propagate_satellites(
                    model,
                    simulation_seconds,
                    position_settings,
                    &propagation_executor);
            }
        }

        if (result
            && !rebuild_tracks
            && controls.selected_track_norad_catalog_id.has_value())
        {
            const auto selected = std::ranges::find_if(*cached_tracks, [&](const SatelliteOrbitTrack& track) {
                return track.norad_catalog_id == *controls.selected_track_norad_catalog_id;
            });
            if (selected != cached_tracks->end()
                && satview_selected_track_needs_refresh(*selected, simulation_seconds))
            {
                auto refreshed = propagate_satellite_track(
                    model,
                    *controls.selected_track_norad_catalog_id,
                    simulation_seconds,
                    controls.track_sample_count);
                if (refreshed.has_value())
                {
                    auto updated_tracks = std::make_shared<std::vector<SatelliteOrbitTrack>>(*cached_tracks);
                    const std::size_t selected_index = static_cast<std::size_t>(
                        std::distance(cached_tracks->begin(), selected));
                    (*updated_tracks)[selected_index] = std::move(*refreshed);
                    cached_tracks = std::move(updated_tracks);
                }
            }
        }
        const double next_simulation_seconds = controls.paused
            ? simulation_seconds
            : simulation_seconds + std::chrono::duration<double>(kPositionTick).count()
                * static_cast<double>(controls.time_speed);
        SatellitePropagationResult next_result;
        if (result && next_simulation_seconds != simulation_seconds)
        {
            SatellitePropagationSettings next_settings;
            next_settings.max_satellites = propagation_settings.max_satellites;
            next_result = propagate_satellites(
                model,
                next_simulation_seconds,
                next_settings,
                &propagation_executor);
        }
        SatViewSimulationSnapshot snapshot;
        snapshot.generation = ++snapshot_generation;
        snapshot.catalog_generation = model_catalog_generation;
        snapshot.simulation_seconds = simulation_seconds;
        snapshot.next_simulation_seconds = next_simulation_seconds;
        snapshot.produced_at = position_sample_time;
        snapshot.time_speed = controls.time_speed;
        snapshot.paused = controls.paused;
        snapshot.source_label = std::string(model.source_label());
        snapshot.source_url = std::string(model.source_url());
        snapshot.states = std::move(result.states);
        if (next_result && next_result.states.size() == snapshot.states.size())
        {
            snapshot.next_teme_positions_km.reserve(next_result.states.size());
            for (std::size_t i = 0; i < next_result.states.size(); ++i)
            {
                if (next_result.states[i].norad_catalog_id != snapshot.states[i].norad_catalog_id)
                {
                    snapshot.next_teme_positions_km.clear();
                    break;
                }
                snapshot.next_teme_positions_km.push_back(next_result.states[i].teme_position_km);
            }
        }
        snapshot.tracks = cached_tracks;
        snapshot.propagation_concurrency = propagation_executor.concurrency();
        snapshot.skipped_model_records = result.skipped_model_records;
        snapshot.failed_propagations = result.failed_propagations;
        snapshot.error = std::move(result.error);
        snapshot.status_text = make_status_text(snapshot);
        snapshots_.publish(std::move(snapshot));

        next_position_time = now + kPositionTick;
    }
}

} // namespace draxul::satview
