#pragma once

#include <draxul/satview/satview_texture_assets.h>

#include <chrono>
#include <cstdint>
#include <draxul/http/http_client.h>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

namespace draxul::satview
{

class SatViewCloudService
{
public:
    using Clock = std::chrono::system_clock;
    using FetchFunction = std::function<std::string(std::string_view url, std::string& error)>;

    enum class DataSource
    {
        None,
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
        std::string source_url;
        std::chrono::seconds refresh_interval = std::chrono::hours(3);
        std::filesystem::path cache_directory;
        FetchFunction fetch;
        std::shared_ptr<http::IHttpClient> http_client;
    };

    struct Status
    {
        DataSource data_source = DataSource::None;
        RefreshState refresh_state = RefreshState::Idle;
        Clock::time_point fetched_at{};
        std::chrono::seconds age = std::chrono::seconds::zero();
        std::string error;
        std::string text;
    };

    SatViewCloudService() = default;
    ~SatViewCloudService();

    SatViewCloudService(const SatViewCloudService&) = delete;
    SatViewCloudService& operator=(const SatViewCloudService&) = delete;

    void start();
    void start(Config config);
    void stop();
    void pump();
    bool request_refresh();

    [[nodiscard]] std::shared_ptr<const LoadedTextureImage> take_pending_image();
    [[nodiscard]] Status status() const;
    [[nodiscard]] std::string status_text() const;

    [[nodiscard]] static std::string default_source_url();
    [[nodiscard]] static std::filesystem::path cache_image_path(const std::filesystem::path& cache_directory);

private:
    struct WorkerResult
    {
        bool success = false;
        DataSource data_source = DataSource::None;
        std::shared_ptr<const LoadedTextureImage> image;
        Clock::time_point fetched_at{};
        Clock::time_point refresh_attempted_at{};
        std::string error;
    };

    void start_refresh(bool force_fetch = false);
    void apply_worker_result(WorkerResult result);
    void update_status_locked(Clock::time_point now);

    mutable std::mutex mutex_;
    Config config_;
    Status status_;
    std::shared_ptr<const LoadedTextureImage> pending_image_;
    std::thread worker_;
    std::optional<WorkerResult> pending_result_;
    bool refresh_in_flight_ = false;
    bool completion_ready_ = false;
    Clock::time_point last_refresh_attempt_{};
    http::CancellationSource cancellation_;
};

} // namespace draxul::satview
