#include <draxul/satview/satview_catalog_service.h>

#include <draxul/log.h>
#include <draxul/perf_timing.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <system_error>
#include <utility>

#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#endif

namespace draxul::satview
{

namespace
{

#ifdef _WIN32
constexpr const char* kNullDevice = "NUL";
#else
constexpr const char* kNullDevice = "/dev/null";
#endif

constexpr const char* kCacheJsonFileName = "celestrak_active_gp.json";
constexpr const char* kCacheMetadataFileName = "celestrak_active_gp.meta";

std::string to_lower_ascii(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (char c : text)
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return out;
}

std::string url_encode_query(std::string_view text)
{
    std::string out;
    char buf[4]{};
    for (unsigned char c : text)
    {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
            || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.')
        {
            out.push_back(static_cast<char>(c));
        }
        else
        {
            std::snprintf(buf, sizeof(buf), "%%%02X", static_cast<unsigned int>(c));
            out += buf;
        }
    }
    return out;
}

std::filesystem::path platform_cache_root()
{
#ifdef _WIN32
    const char* local_appdata = std::getenv("LOCALAPPDATA");
    const char* appdata = std::getenv("APPDATA");
    const std::filesystem::path base = (local_appdata && local_appdata[0] != '\0')
        ? std::filesystem::path(local_appdata)
        : ((appdata && appdata[0] != '\0') ? std::filesystem::path(appdata) : std::filesystem::path("."));
    return base / "draxul" / "cache";
#elif defined(__APPLE__)
    const char* home = std::getenv("HOME");
    const std::filesystem::path base = (home && home[0] != '\0') ? std::filesystem::path(home) : std::filesystem::path(".");
    return base / "Library" / "Application Support" / "draxul" / "Cache";
#else
    const char* xdg = std::getenv("XDG_CACHE_HOME");
    const char* home = std::getenv("HOME");
    if (xdg && xdg[0] != '\0')
        return std::filesystem::path(xdg) / "draxul";
    if (home && home[0] != '\0')
        return std::filesystem::path(home) / ".cache" / "draxul";
    return std::filesystem::path(".") / ".cache" / "draxul";
#endif
}

std::string run_curl_fetch(std::string_view url, std::string& error)
{
    std::string cmd = "curl -L --fail --silent --show-error --max-time 10 --connect-timeout 5 --user-agent \"Draxul SatView\" \"";
    cmd += std::string(url);
    cmd += "\" 2>";
    cmd += kNullDevice;

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe)
    {
        error = "failed to start curl";
        return {};
    }

    std::string result;
    std::array<char, 8192> buf{};
    while (auto n = fread(buf.data(), 1, buf.size(), pipe))
        result.append(buf.data(), n);
    const int status = pclose(pipe);
    if (status != 0)
    {
        error = "curl failed";
        return {};
    }
    if (result.empty())
        error = "empty response";
    return result;
}

bool write_text_atomic(const std::filesystem::path& path, std::string_view content, std::string& error)
{
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec)
    {
        error = "failed to create cache directory: " + ec.message();
        return false;
    }

    const std::filesystem::path tmp = path.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out.is_open())
        {
            error = "failed to open " + tmp.string();
            return false;
        }
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
        if (!out.good())
        {
            error = "failed to write " + tmp.string();
            return false;
        }
    }

    std::filesystem::rename(tmp, path, ec);
    if (ec)
    {
        std::filesystem::remove(path, ec);
        ec.clear();
        std::filesystem::rename(tmp, path, ec);
        if (ec)
        {
            error = "failed to replace " + path.string() + ": " + ec.message();
            return false;
        }
    }
    return true;
}

std::optional<std::string> read_text_file(const std::filesystem::path& path, std::string& error)
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

std::string epoch_range_min(const SatelliteCatalog& catalog)
{
    std::string min_epoch;
    for (const auto& object : catalog.objects)
    {
        if (object.epoch_utc.empty())
            continue;
        if (min_epoch.empty() || object.epoch_utc < min_epoch)
            min_epoch = object.epoch_utc;
    }
    return min_epoch;
}

std::string epoch_range_max(const SatelliteCatalog& catalog)
{
    std::string max_epoch;
    for (const auto& object : catalog.objects)
    {
        if (object.epoch_utc.empty())
            continue;
        if (max_epoch.empty() || object.epoch_utc > max_epoch)
            max_epoch = object.epoch_utc;
    }
    return max_epoch;
}

std::int64_t unix_seconds(SatViewCatalogService::Clock::time_point time)
{
    return std::chrono::duration_cast<std::chrono::seconds>(time.time_since_epoch()).count();
}

SatViewCatalogService::Clock::time_point time_from_unix_seconds(std::int64_t seconds)
{
    return SatViewCatalogService::Clock::time_point(std::chrono::seconds(seconds));
}

std::string format_age(std::chrono::seconds age)
{
    if (age < std::chrono::minutes(1))
        return std::to_string(std::max<std::int64_t>(0, age.count())) + "s";
    const auto minutes = std::chrono::duration_cast<std::chrono::minutes>(age);
    if (minutes < std::chrono::hours(1))
        return std::to_string(minutes.count()) + "m";
    const auto hours = std::chrono::duration_cast<std::chrono::hours>(age);
    if (hours < std::chrono::hours(48))
        return std::to_string(hours.count()) + "h";
    return std::to_string(hours.count() / 24) + "d";
}

} // namespace

namespace
{

std::string serialize_metadata(const SatViewCatalogService::CacheMetadata& meta)
{
    std::ostringstream out;
    out << "source_label=" << meta.source_label << "\n";
    out << "source_url=" << meta.source_url << "\n";
    out << "fetched_unix=" << unix_seconds(meta.fetched_at) << "\n";
    out << "object_count=" << meta.object_count << "\n";
    out << "skipped_records=" << meta.skipped_records << "\n";
    out << "epoch_min=" << meta.epoch_min << "\n";
    out << "epoch_max=" << meta.epoch_max << "\n";
    return out.str();
}

std::optional<SatViewCatalogService::CacheMetadata> parse_metadata(std::string_view text)
{
    SatViewCatalogService::CacheMetadata meta;
    bool have_fetched = false;

    size_t pos = 0;
    while (pos <= text.size())
    {
        size_t end = text.find('\n', pos);
        if (end == std::string_view::npos)
            end = text.size();
        std::string_view line = text.substr(pos, end - pos);
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);

        const size_t eq = line.find('=');
        if (eq != std::string_view::npos)
        {
            const std::string key(line.substr(0, eq));
            const std::string value(line.substr(eq + 1));
            if (key == "source_label")
                meta.source_label = value;
            else if (key == "source_url")
                meta.source_url = value;
            else if (key == "fetched_unix")
            {
                char* parse_end = nullptr;
                const auto parsed = std::strtoll(value.c_str(), &parse_end, 10);
                if (parse_end != value.c_str())
                {
                    meta.fetched_at = time_from_unix_seconds(parsed);
                    have_fetched = true;
                }
            }
            else if (key == "object_count")
                meta.object_count = static_cast<std::size_t>(std::strtoull(value.c_str(), nullptr, 10));
            else if (key == "skipped_records")
                meta.skipped_records = static_cast<std::size_t>(std::strtoull(value.c_str(), nullptr, 10));
            else if (key == "epoch_min")
                meta.epoch_min = value;
            else if (key == "epoch_max")
                meta.epoch_max = value;
        }

        if (end == text.size())
            break;
        pos = end + 1;
    }

    if (!have_fetched)
        return std::nullopt;
    return meta;
}

std::optional<SatViewCatalogService::CacheMetadata> read_metadata(
    const std::filesystem::path& path,
    std::string& error)
{
    auto content = read_text_file(path, error);
    if (!content.has_value())
        return std::nullopt;
    auto meta = parse_metadata(*content);
    if (!meta.has_value())
        error = "invalid cache metadata";
    return meta;
}

bool write_cache_files(
    const std::filesystem::path& cache_dir,
    const SatViewCatalogService::WorkerResult& result,
    std::string& error)
{
    const auto json_path = cache_dir / kCacheJsonFileName;
    const auto meta_path = cache_dir / kCacheMetadataFileName;
    if (!write_text_atomic(json_path, result.raw_json, error))
        return false;

    SatViewCatalogService::CacheMetadata meta;
    meta.source_label = result.catalog.source_label;
    meta.source_url = result.catalog.source_url;
    meta.fetched_at = result.fetched_at;
    meta.object_count = result.catalog.objects.size();
    meta.skipped_records = result.catalog.skipped_records;
    meta.epoch_min = epoch_range_min(result.catalog);
    meta.epoch_max = epoch_range_max(result.catalog);
    return write_text_atomic(meta_path, serialize_metadata(meta), error);
}

std::optional<std::pair<SatelliteCatalog, SatViewCatalogService::CacheMetadata>> read_cache_files(
    const std::filesystem::path& cache_dir,
    std::string& error)
{
    const auto json_path = cache_dir / kCacheJsonFileName;
    const auto meta_path = cache_dir / kCacheMetadataFileName;
    auto meta = read_metadata(meta_path, error);
    if (!meta.has_value())
        return std::nullopt;

    auto json = read_text_file(json_path, error);
    if (!json.has_value())
        return std::nullopt;

    auto parsed = parse_celestrak_gp_json(*json, meta->source_label, meta->source_url);
    if (!parsed)
    {
        error = parsed.error;
        return std::nullopt;
    }
    parsed.catalog.skipped_records = std::max(parsed.catalog.skipped_records, meta->skipped_records);
    return std::make_pair(std::move(parsed.catalog), *meta);
}

} // namespace

SatViewCatalogService::SatViewCatalogService(Config config)
{
    start(std::move(config));
}

SatViewCatalogService::~SatViewCatalogService()
{
    stop();
}

void SatViewCatalogService::start(Config config)
{
    PERF_MEASURE();
    stop();

    if (config.cache_directory.empty())
        config.cache_directory = default_cache_directory();
    if (config.celestrak_group.empty())
        config.celestrak_group = "active";
    if (!config.fetch)
        config.fetch = run_curl_fetch;

    config_ = std::move(config);
    const auto now = Clock::now();
    bool should_refresh = true;
    std::string cache_error;

    {
        std::lock_guard lock(mutex_);
        catalog_ = {};
        status_ = {};
        catalog_generation_ = 0;
    }

    if (auto cached = read_cache_files(config_.cache_directory, cache_error))
    {
        const auto age = std::chrono::duration_cast<std::chrono::seconds>(now - cached->second.fetched_at);
        {
            std::lock_guard lock(mutex_);
            catalog_ = std::move(cached->first);
            ++catalog_generation_;
            status_.data_source = DataSource::Cache;
            status_.refresh_state = RefreshState::Idle;
            status_.fetched_at = cached->second.fetched_at;
            status_.cache_age = age;
            status_.source_label = cached->second.source_label.empty() ? config_.celestrak_group : cached->second.source_label;
            status_.source_url = cached->second.source_url;
            status_.object_count = catalog_.objects.size();
            status_.skipped_records = catalog_.skipped_records;
            publish_status_locked();
        }
        should_refresh = age >= config_.refresh_interval;
    }
    else
    {
        auto sample = load_sample_satellite_catalog();
        std::lock_guard lock(mutex_);
        if (sample)
        {
            catalog_ = std::move(sample.catalog);
            ++catalog_generation_;
            status_.data_source = DataSource::Sample;
            status_.object_count = catalog_.objects.size();
            status_.skipped_records = catalog_.skipped_records;
            status_.source_label = catalog_.source_label;
            status_.source_url = catalog_.source_url;
        }
        else
        {
            status_.data_source = DataSource::None;
            status_.error = sample.error.empty() ? cache_error : sample.error;
        }
        status_.refresh_state = RefreshState::Idle;
        publish_status_locked();
    }

    if (should_refresh)
        start_refresh(false);
}

void SatViewCatalogService::stop()
{
    if (worker_.joinable())
        worker_.join();
    std::lock_guard lock(mutex_);
    refresh_in_flight_ = false;
    completion_ready_ = false;
    pending_result_.reset();
}

void SatViewCatalogService::pump()
{
    PERF_MEASURE();
    std::optional<WorkerResult> result;
    bool should_join = false;
    {
        std::lock_guard lock(mutex_);
        if (completion_ready_)
        {
            completion_ready_ = false;
            result = std::move(pending_result_);
            pending_result_.reset();
            should_join = true;
        }
    }

    if (should_join && worker_.joinable())
        worker_.join();
    if (result.has_value())
        apply_worker_result(std::move(*result));
}

bool SatViewCatalogService::request_refresh()
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

SatelliteCatalog SatViewCatalogService::catalog() const
{
    std::lock_guard lock(mutex_);
    return catalog_;
}

std::uint64_t SatViewCatalogService::catalog_generation() const
{
    std::lock_guard lock(mutex_);
    return catalog_generation_;
}

SatViewCatalogService::Status SatViewCatalogService::status() const
{
    std::lock_guard lock(mutex_);
    return status_;
}

std::string SatViewCatalogService::status_text() const
{
    std::lock_guard lock(mutex_);
    return status_.text;
}

bool SatViewCatalogService::refresh_in_flight() const
{
    std::lock_guard lock(mutex_);
    return refresh_in_flight_;
}

std::filesystem::path SatViewCatalogService::default_cache_directory()
{
    return platform_cache_root() / "satview";
}

std::string SatViewCatalogService::default_celestrak_url(std::string_view group)
{
    return "https://celestrak.org/NORAD/elements/gp.php?GROUP="
        + url_encode_query(to_lower_ascii(group)) + "&FORMAT=json";
}

void SatViewCatalogService::start_refresh(bool force)
{
    PERF_MEASURE();
    Config config;
    std::string url;
    {
        std::lock_guard lock(mutex_);
        if (refresh_in_flight_)
            return;
        config = config_;
        url = default_celestrak_url(config.celestrak_group);
        status_.refresh_state = status_.data_source == DataSource::None ? RefreshState::Loading : RefreshState::Refreshing;
        status_.error.clear();
        refresh_in_flight_ = true;
        completion_ready_ = false;
        publish_status_locked();
    }

    worker_ = std::thread([this, config = std::move(config), url = std::move(url), force]() mutable {
        (void)force;
        WorkerResult result;
        result.fetched_at = Clock::now();
        std::string fetch_error;
        result.raw_json = config.fetch(url, fetch_error);
        if (result.raw_json.empty())
        {
            result.error = fetch_error.empty() ? "empty CelesTrak response" : fetch_error;
        }
        else
        {
            auto parsed = parse_celestrak_gp_json(result.raw_json, config.celestrak_group, url);
            if (!parsed)
            {
                result.error = parsed.error;
            }
            else
            {
                result.success = true;
                result.catalog = std::move(parsed.catalog);
                std::string cache_error;
                if (!write_cache_files(config.cache_directory, result, cache_error))
                {
                    DRAXUL_LOG_WARN(LogCategory::Renderer,
                        "SatView: failed to write catalog cache: %s",
                        cache_error.c_str());
                }
            }
        }

        std::lock_guard lock(mutex_);
        pending_result_ = std::move(result);
        refresh_in_flight_ = false;
        completion_ready_ = true;
    });
}

void SatViewCatalogService::apply_worker_result(WorkerResult result)
{
    std::lock_guard lock(mutex_);
    if (result.success)
    {
        catalog_ = std::move(result.catalog);
        ++catalog_generation_;
        status_.data_source = DataSource::Live;
        status_.refresh_state = RefreshState::Idle;
        status_.object_count = catalog_.objects.size();
        status_.skipped_records = catalog_.skipped_records;
        status_.fetched_at = result.fetched_at;
        status_.cache_age = std::chrono::seconds::zero();
        status_.source_label = catalog_.source_label;
        status_.source_url = catalog_.source_url;
        status_.error.clear();
    }
    else
    {
        status_.refresh_state = RefreshState::Failed;
        status_.error = std::move(result.error);
    }
    publish_status_locked();
}

void SatViewCatalogService::publish_status_locked()
{
    if (status_.data_source == DataSource::Cache && status_.fetched_at != Clock::time_point{})
    {
        const auto now = Clock::now();
        status_.cache_age = std::chrono::duration_cast<std::chrono::seconds>(now - status_.fetched_at);
    }

    std::string source;
    switch (status_.data_source)
    {
    case DataSource::Live:
        source = "live";
        break;
    case DataSource::Cache:
        source = "cached";
        break;
    case DataSource::Sample:
        source = "sample";
        break;
    case DataSource::None:
        source = "catalog";
        break;
    }

    if (status_.data_source == DataSource::None)
    {
        status_.text = "catalog loading";
    }
    else
    {
        status_.text = source + " " + std::to_string(status_.object_count) + " sats";
        if (status_.data_source == DataSource::Cache)
            status_.text += " " + format_age(status_.cache_age) + " old";
    }

    if (status_.refresh_state == RefreshState::Loading)
    {
        status_.text += " | loading";
    }
    else if (status_.refresh_state == RefreshState::Refreshing)
    {
        status_.text += " | refreshing";
    }
    else if (status_.refresh_state == RefreshState::Failed)
    {
        status_.text += " | fetch failed";
    }
}

} // namespace draxul::satview
