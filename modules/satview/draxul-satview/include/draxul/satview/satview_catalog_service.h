#pragma once

#include <chrono>
#include <cstdint>
#include <draxul/satview/satview_catalog.h>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace draxul::satview
{

class SatViewCatalogService
{
public:
    using Clock = std::chrono::system_clock;
    using FetchFunction = std::function<std::string(std::string_view url, std::string& error)>;

    enum class DataSource
    {
        None,
        Sample,
        Cache,
        Live
    };

    enum class RefreshState
    {
        Idle,
        Loading,
        Refreshing,
        Failed
    };

    struct Config
    {
        std::string celestrak_group = "active";
        std::chrono::seconds refresh_interval = std::chrono::hours(2);
        std::filesystem::path cache_directory;
        FetchFunction fetch;
    };

    struct Status
    {
        DataSource data_source = DataSource::None;
        RefreshState refresh_state = RefreshState::Idle;
        std::size_t object_count = 0;
        std::size_t skipped_records = 0;
        std::chrono::seconds cache_age = std::chrono::seconds::zero();
        Clock::time_point fetched_at{};
        std::string source_label;
        std::string source_url;
        std::string error;
        std::string text;
    };

    struct CacheMetadata
    {
        std::string source_label;
        std::string source_url;
        Clock::time_point fetched_at{};
        std::size_t object_count = 0;
        std::size_t skipped_records = 0;
        std::string epoch_min;
        std::string epoch_max;
    };

    struct WorkerResult
    {
        bool success = false;
        SatelliteCatalog catalog;
        std::string raw_json;
        std::string error;
        Clock::time_point fetched_at{};
    };

    SatViewCatalogService() = default;
    explicit SatViewCatalogService(Config config);
    ~SatViewCatalogService();

    SatViewCatalogService(const SatViewCatalogService&) = delete;
    SatViewCatalogService& operator=(const SatViewCatalogService&) = delete;

    void start(Config config = {});
    void stop();
    void pump();
    bool request_refresh();

    [[nodiscard]] SatelliteCatalog catalog() const;
    [[nodiscard]] std::uint64_t catalog_generation() const;
    [[nodiscard]] Status status() const;
    [[nodiscard]] std::string status_text() const;
    [[nodiscard]] bool refresh_in_flight() const;

    [[nodiscard]] static std::filesystem::path default_cache_directory();
    [[nodiscard]] static std::string default_celestrak_url(std::string_view group);

private:
    void start_refresh(bool force);
    void apply_worker_result(WorkerResult result);
    void publish_status_locked();

    mutable std::mutex mutex_;
    Config config_;
    SatelliteCatalog catalog_;
    Status status_;
    std::uint64_t catalog_generation_ = 0;

    std::thread worker_;
    std::optional<WorkerResult> pending_result_;
    bool refresh_in_flight_ = false;
    bool completion_ready_ = false;
};

} // namespace draxul::satview
