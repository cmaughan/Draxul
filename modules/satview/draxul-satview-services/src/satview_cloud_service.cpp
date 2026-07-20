#include <draxul/satview/satview_cloud_service.h>

#include <draxul/log.h>
#include <draxul/perf_timing.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>

namespace draxul::satview
{

namespace
{

constexpr std::size_t kMaxCloudResponseBytes = 128 * 1024 * 1024;

std::optional<std::string> read_binary_file(const std::filesystem::path& path, std::string& error)
{
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open())
    {
        error = "failed to open " + path.string();
        return std::nullopt;
    }
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (in.bad())
    {
        error = "failed to read " + path.string();
        return std::nullopt;
    }
    return content;
}

bool write_binary_atomic(const std::filesystem::path& path, std::string_view content, std::string& error)
{
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec)
    {
        error = "failed to create cloud cache directory: " + ec.message();
        return false;
    }

    const std::filesystem::path temporary = path.string() + ".tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out.is_open())
        {
            error = "failed to open " + temporary.string();
            return false;
        }
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
        if (!out.good())
        {
            error = "failed to write " + temporary.string();
            return false;
        }
    }

    std::filesystem::rename(temporary, path, ec);
    if (ec)
    {
        std::filesystem::remove(path, ec);
        ec.clear();
        std::filesystem::rename(temporary, path, ec);
    }
    if (ec)
    {
        error = "failed to replace cloud cache: " + ec.message();
        return false;
    }
    return true;
}

SatViewCloudService::Clock::time_point file_time_to_system_time(std::filesystem::file_time_type file_time)
{
    return SatViewCloudService::Clock::now()
        + std::chrono::duration_cast<SatViewCloudService::Clock::duration>(
            file_time - std::filesystem::file_time_type::clock::now());
}

std::string format_age(std::chrono::seconds age)
{
    if (age < std::chrono::minutes(1))
        return "just now";
    if (age < std::chrono::hours(1))
        return std::to_string(std::chrono::duration_cast<std::chrono::minutes>(age).count()) + "m old";
    return std::to_string(std::chrono::duration_cast<std::chrono::hours>(age).count()) + "h old";
}

} // namespace

SatViewCloudService::~SatViewCloudService()
{
    stop();
}

void SatViewCloudService::start()
{
    start(Config{});
}

void SatViewCloudService::start(Config config)
{
    stop();
    if (config.source_url.empty())
        config.source_url = default_source_url();
    if (!config.fetch && !config.http_client)
        config.http_client = http::create_platform_http_client();
    config_ = std::move(config);
    {
        std::lock_guard lock(mutex_);
        status_ = {};
        pending_image_.reset();
        pending_result_.reset();
        completion_ready_ = false;
        last_refresh_attempt_ = {};
    }
    start_refresh(false);
}

void SatViewCloudService::stop()
{
    cancellation_.cancel();
    if (worker_.joinable())
        worker_.join();
    std::lock_guard lock(mutex_);
    refresh_in_flight_ = false;
    completion_ready_ = false;
    pending_result_.reset();
}

void SatViewCloudService::pump()
{
    PERF_MEASURE();
    std::optional<WorkerResult> result;
    {
        std::lock_guard lock(mutex_);
        if (completion_ready_)
        {
            completion_ready_ = false;
            result = std::move(pending_result_);
            pending_result_.reset();
        }
    }

    if (result.has_value())
    {
        if (worker_.joinable())
            worker_.join();
        apply_worker_result(std::move(*result));
    }

    bool refresh_due = false;
    {
        std::lock_guard lock(mutex_);
        const auto now = Clock::now();
        update_status_locked(now);
        const auto refresh_anchor = std::max(status_.fetched_at, last_refresh_attempt_);
        refresh_due = !refresh_in_flight_
            && refresh_anchor != Clock::time_point{}
            && now - refresh_anchor >= config_.refresh_interval;
    }
    if (refresh_due)
        start_refresh(true);
}

bool SatViewCloudService::request_refresh()
{
    pump();
    {
        std::lock_guard lock(mutex_);
        if (refresh_in_flight_)
            return false;
    }
    start_refresh(true);
    return true;
}

std::shared_ptr<const LoadedTextureImage> SatViewCloudService::take_pending_image()
{
    std::lock_guard lock(mutex_);
    return std::exchange(pending_image_, {});
}

SatViewCloudService::Status SatViewCloudService::status() const
{
    std::lock_guard lock(mutex_);
    Status result = status_;
    if (result.fetched_at != Clock::time_point{})
        result.age = std::chrono::duration_cast<std::chrono::seconds>(Clock::now() - result.fetched_at);
    return result;
}

std::string SatViewCloudService::status_text() const
{
    return status().text;
}

std::string SatViewCloudService::default_source_url()
{
    return "https://clouds.matteason.co.uk/images/8192x4096/clouds.jpg";
}

std::filesystem::path SatViewCloudService::cache_image_path(const std::filesystem::path& cache_directory)
{
    return cache_directory / "live_clouds_8192x4096.jpg";
}

void SatViewCloudService::start_refresh(bool force_fetch)
{
    Config config;
    {
        std::lock_guard lock(mutex_);
        if (refresh_in_flight_)
            return;
        config = config_;
        cancellation_ = http::CancellationSource{};
        refresh_in_flight_ = true;
        completion_ready_ = false;
        status_.refresh_state = status_.data_source == DataSource::None
            ? RefreshState::Loading
            : RefreshState::Refreshing;
        update_status_locked(Clock::now());
    }

    const http::CancellationToken cancellation = cancellation_.token();
    worker_ = std::thread([this, config = std::move(config), force_fetch, cancellation]() mutable {
        WorkerResult result;
        const auto cache_path = cache_image_path(config.cache_directory);
        std::error_code ec;
        const bool cache_exists = std::filesystem::exists(cache_path, ec) && !ec;
        Clock::time_point cache_time{};
        if (cache_exists)
            cache_time = file_time_to_system_time(std::filesystem::last_write_time(cache_path, ec));
        const bool cache_fresh = cache_exists && !ec
            && Clock::now() - cache_time < config.refresh_interval;

        if (cache_fresh && !force_fetch)
        {
            auto image = std::make_shared<LoadedTextureImage>(load_rgba8_image(cache_path));
            if (image->valid())
            {
                result.success = true;
                result.data_source = DataSource::Cache;
                result.image = std::move(image);
                result.fetched_at = cache_time;
            }
            else
            {
                result.error = "cached cloud image is invalid";
            }
        }
        else
        {
            result.refresh_attempted_at = Clock::now();
            std::string fetch_error;
            std::string bytes;
            if (config.fetch)
            {
                bytes = config.fetch(config.source_url, fetch_error);
            }
            else
            {
                http::Request request;
                request.url = config.source_url;
                request.user_agent = "Draxul SatView";
                request.connect_timeout = std::chrono::seconds(10);
                request.overall_timeout = std::chrono::seconds(60);
                request.max_response_bytes = kMaxCloudResponseBytes;
                auto response = config.http_client->get(request, cancellation);
                if (response.ok())
                    bytes = std::move(response.body);
                else
                    fetch_error = std::move(response.error);
            }
            std::string cache_error;
            if (!bytes.empty() && write_binary_atomic(cache_path, bytes, cache_error))
            {
                auto image = std::make_shared<LoadedTextureImage>(load_rgba8_image(cache_path));
                if (image->valid())
                {
                    result.success = true;
                    result.data_source = DataSource::Live;
                    result.image = std::move(image);
                    result.fetched_at = Clock::now();
                }
                else
                {
                    result.error = "downloaded cloud image is invalid";
                }
            }
            else
            {
                result.error = !fetch_error.empty() ? std::move(fetch_error) : std::move(cache_error);
            }

            if (!result.success && cache_exists)
            {
                auto image = std::make_shared<LoadedTextureImage>(load_rgba8_image(cache_path));
                if (image->valid())
                {
                    result.success = true;
                    result.data_source = DataSource::Cache;
                    result.image = std::move(image);
                    result.fetched_at = cache_time;
                }
            }
        }

        std::lock_guard lock(mutex_);
        pending_result_ = std::move(result);
        refresh_in_flight_ = false;
        completion_ready_ = true;
    });
}

void SatViewCloudService::apply_worker_result(WorkerResult result)
{
    std::lock_guard lock(mutex_);
    if (result.refresh_attempted_at != Clock::time_point{})
        last_refresh_attempt_ = result.refresh_attempted_at;
    if (result.success)
    {
        status_.data_source = result.data_source;
        status_.refresh_state = result.error.empty() ? RefreshState::Idle : RefreshState::Failed;
        status_.fetched_at = result.fetched_at;
        status_.error = std::move(result.error);
        pending_image_ = std::move(result.image);
    }
    else
    {
        status_.refresh_state = RefreshState::Failed;
        status_.error = std::move(result.error);
        DRAXUL_LOG_WARN(LogCategory::Renderer, "SatView: live cloud fetch failed: %s", status_.error.c_str());
    }
    update_status_locked(Clock::now());
}

void SatViewCloudService::update_status_locked(Clock::time_point now)
{
    if (status_.fetched_at != Clock::time_point{})
        status_.age = std::chrono::duration_cast<std::chrono::seconds>(now - status_.fetched_at);

    if (status_.refresh_state == RefreshState::Loading)
        status_.text = "loading live clouds";
    else if (status_.refresh_state == RefreshState::Refreshing)
        status_.text = "refreshing live clouds";
    else if (status_.refresh_state == RefreshState::Failed && status_.data_source == DataSource::None)
        status_.text = "bundled clouds; live unavailable: " + status_.error;
    else if (status_.data_source == DataSource::Live)
        status_.text = "live clouds " + format_age(status_.age);
    else if (status_.data_source == DataSource::Cache)
        status_.text = "cached clouds " + format_age(status_.age);
    else
        status_.text = "bundled cloud texture";

    if (status_.refresh_state == RefreshState::Failed && status_.data_source != DataSource::None)
        status_.text += "; refresh failed";
}

} // namespace draxul::satview
