#include <draxul/satview/satview_catalog_service.h>

#include <draxul/log.h>
#include <draxul/perf_timing.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <system_error>
#include <utility>

namespace draxul::satview
{

namespace
{

constexpr const char* kDefaultCelestrakGroup = "active";
constexpr std::size_t kMaxCatalogResponseBytes = 64 * 1024 * 1024;

std::string to_lower_ascii(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (char c : text)
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return out;
}

std::string cache_group_slug(std::string_view group)
{
    std::string out;
    out.reserve(group.size());
    for (unsigned char c : to_lower_ascii(group))
    {
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.')
            out.push_back(static_cast<char>(c));
        else
            out.push_back('_');
    }
    return out.empty() ? std::string(kDefaultCelestrakGroup) : out;
}

std::filesystem::path cache_json_path(const std::filesystem::path& cache_dir, std::string_view group)
{
    return cache_dir / ("celestrak_" + cache_group_slug(group) + "_gp.json");
}

std::filesystem::path cache_metadata_path(const std::filesystem::path& cache_dir, std::string_view group)
{
    return cache_dir / ("celestrak_" + cache_group_slug(group) + "_gp.meta");
}

std::filesystem::path satcat_csv_path(const std::filesystem::path& cache_dir)
{
    return cache_dir / "celestrak_satcat.csv";
}

std::filesystem::path satcat_metadata_path(const std::filesystem::path& cache_dir)
{
    return cache_dir / "celestrak_satcat.meta";
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

std::optional<std::string> read_text_file(const std::filesystem::path& path, std::string& error);

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
    const std::filesystem::path& payload_path,
    const std::filesystem::path& meta_path,
    std::string_view raw_payload,
    const SatelliteCatalog& catalog,
    SatViewCatalogService::Clock::time_point fetched_at,
    std::string& error)
{
    if (!write_text_atomic(payload_path, raw_payload, error))
        return false;

    SatViewCatalogService::CacheMetadata meta;
    meta.source_label = catalog.source_label;
    meta.source_url = catalog.source_url;
    meta.fetched_at = fetched_at;
    meta.object_count = catalog.objects.size();
    meta.skipped_records = catalog.skipped_records;
    meta.epoch_min = epoch_range_min(catalog);
    meta.epoch_max = epoch_range_max(catalog);
    return write_text_atomic(meta_path, serialize_metadata(meta), error);
}

template <typename ParseFunction>
std::optional<std::pair<SatelliteCatalog, SatViewCatalogService::CacheMetadata>> read_cache_files(
    const std::filesystem::path& payload_path,
    const std::filesystem::path& meta_path,
    ParseFunction&& parse,
    std::string& error)
{
    auto meta = read_metadata(meta_path, error);
    if (!meta.has_value())
        return std::nullopt;

    auto payload = read_text_file(payload_path, error);
    if (!payload.has_value())
        return std::nullopt;

    auto parsed = parse(*payload, meta->source_label, meta->source_url);
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

void SatViewCatalogService::start()
{
    start(Config{});
}

void SatViewCatalogService::start(Config config)
{
    PERF_MEASURE();
    stop();

    if (config.cache_directory.empty())
        config.cache_directory = default_cache_directory();
    if (config.celestrak_group.empty())
        config.celestrak_group = kDefaultCelestrakGroup;
    if (config.satcat_url.empty())
        config.satcat_url = default_satcat_url();
    if (!config.fetch && !config.http_client)
        config.http_client = http::create_platform_http_client();

    config_ = std::move(config);
    const auto now = Clock::now();
    SatelliteCatalog gp_catalog;
    SatelliteCatalog satcat_catalog;
    SourceStatus gp_status;
    SourceStatus satcat_status;
    std::string gp_cache_error;
    std::string satcat_cache_error;
    std::string startup_error;

    auto gp_cached = read_cache_files(
        cache_json_path(config_.cache_directory, config_.celestrak_group),
        cache_metadata_path(config_.cache_directory, config_.celestrak_group),
        parse_celestrak_gp_json,
        gp_cache_error);
    if (gp_cached)
    {
        gp_catalog = std::move(gp_cached->first);
        gp_status.data_source = DataSource::Cache;
        gp_status.fetched_at = gp_cached->second.fetched_at;
        gp_status.cache_age = std::chrono::duration_cast<std::chrono::seconds>(now - gp_status.fetched_at);
        gp_status.source_label = gp_cached->second.source_label;
        gp_status.source_url = gp_cached->second.source_url;
        gp_status.object_count = gp_catalog.objects.size();
        gp_status.malformed_records = gp_catalog.malformed_records;
        gp_status.excluded_records = gp_catalog.excluded_records;
        gp_status.non_renderable_records = gp_catalog.non_renderable_records;
    }

    auto satcat_cached = read_cache_files(
        satcat_csv_path(config_.cache_directory),
        satcat_metadata_path(config_.cache_directory),
        parse_celestrak_satcat_csv,
        satcat_cache_error);
    if (satcat_cached)
    {
        satcat_catalog = std::move(satcat_cached->first);
        std::string disposition_error;
        const std::size_t disposition_count =
            load_bundled_lunar_dispositions(satcat_catalog, &disposition_error);
        if (disposition_count > 0)
            DRAXUL_LOG_DEBUG(LogCategory::Renderer,
                "SatView: excluded %zu confirmed non-orbiting lunar objects",
                disposition_count);
        if (!disposition_error.empty())
            DRAXUL_LOG_WARN(LogCategory::Renderer, "SatView: %s", disposition_error.c_str());
        satcat_status.data_source = DataSource::Cache;
        satcat_status.fetched_at = satcat_cached->second.fetched_at;
        satcat_status.cache_age = std::chrono::duration_cast<std::chrono::seconds>(now - satcat_status.fetched_at);
        satcat_status.source_label = satcat_cached->second.source_label;
        satcat_status.source_url = satcat_cached->second.source_url;
        satcat_status.object_count = satcat_catalog.objects.size();
        satcat_status.malformed_records = satcat_catalog.malformed_records;
        satcat_status.excluded_records = satcat_catalog.excluded_records;
        satcat_status.non_renderable_records = satcat_catalog.non_renderable_records;
    }

    SatelliteCatalog merged = merge_satellite_catalogs(gp_catalog, satcat_catalog);
    std::string ephemeris_error;
    const std::size_t ephemeris_count = load_bundled_sampled_ephemeris(merged, &ephemeris_error);
    if (ephemeris_count > 0)
        DRAXUL_LOG_DEBUG(LogCategory::Renderer,
            "SatView: applied sampled ephemerides to %zu lunar objects", ephemeris_count);
    if (!ephemeris_error.empty())
        DRAXUL_LOG_WARN(LogCategory::Renderer, "SatView: %s", ephemeris_error.c_str());
    if (merged.objects.empty())
    {
        auto sample = load_sample_satellite_catalog();
        if (sample)
            merged = std::move(sample.catalog);
        else
            startup_error = sample.error;
    }

    {
        std::lock_guard lock(mutex_);
        gp_catalog_ = std::move(gp_catalog);
        satcat_catalog_ = std::move(satcat_catalog);
        catalog_ = std::move(merged);
        status_ = {};
        status_.gp = std::move(gp_status);
        status_.satcat = std::move(satcat_status);
        status_.error = std::move(startup_error);
        if (catalog_.source_label == "sample")
            status_.data_source = DataSource::Sample;
        catalog_generation_ = catalog_.objects.empty() ? 0 : 1;
        publish_status_locked();
    }

    const bool should_refresh_gp = !gp_cached
        || std::chrono::duration_cast<std::chrono::seconds>(now - gp_cached->second.fetched_at)
            >= config_.refresh_interval;
    const bool should_refresh_satcat = !satcat_cached
        || std::chrono::duration_cast<std::chrono::seconds>(now - satcat_cached->second.fetched_at)
            >= config_.satcat_refresh_interval;
    if (should_refresh_gp || should_refresh_satcat)
        start_refresh();
}

void SatViewCatalogService::stop()
{
    cancellation_.cancel();
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
        const auto now = Clock::now();
        if (has_fresh_source_locked(status_.gp, config_.refresh_interval, now)
            && has_fresh_source_locked(status_.satcat, config_.satcat_refresh_interval, now))
        {
            return false;
        }
    }
    start_refresh();
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
        + http::encode_query_component(to_lower_ascii(group)) + "&FORMAT=json";
}

std::string SatViewCatalogService::default_satcat_url()
{
    return "https://celestrak.org/pub/satcat.csv";
}

void SatViewCatalogService::start_refresh()
{
    PERF_MEASURE();
    Config config;
    SatelliteCatalog gp_catalog;
    SatelliteCatalog satcat_catalog;
    bool refresh_gp = false;
    bool refresh_satcat = false;
    {
        std::lock_guard lock(mutex_);
        if (refresh_in_flight_)
            return;
        const auto now = Clock::now();
        refresh_gp = !has_fresh_source_locked(status_.gp, config_.refresh_interval, now);
        refresh_satcat = !has_fresh_source_locked(status_.satcat, config_.satcat_refresh_interval, now);
        if (!refresh_gp && !refresh_satcat)
            return;
        config = config_;
        cancellation_ = http::CancellationSource{};
        gp_catalog = gp_catalog_;
        satcat_catalog = satcat_catalog_;
        status_.refresh_state = status_.data_source == DataSource::None ? RefreshState::Loading : RefreshState::Refreshing;
        if (refresh_gp)
        {
            status_.gp.refresh_state = status_.gp.data_source == DataSource::None
                ? RefreshState::Loading
                : RefreshState::Refreshing;
            status_.gp.error.clear();
        }
        if (refresh_satcat)
        {
            status_.satcat.refresh_state = status_.satcat.data_source == DataSource::None
                ? RefreshState::Loading
                : RefreshState::Refreshing;
            status_.satcat.error.clear();
        }
        refresh_in_flight_ = true;
        completion_ready_ = false;
        publish_status_locked();
    }

    const http::CancellationToken cancellation = cancellation_.token();
    worker_ = std::thread([this,
                              config = std::move(config),
                              gp_catalog = std::move(gp_catalog),
                              satcat_catalog = std::move(satcat_catalog),
                              refresh_gp,
                              refresh_satcat,
                              cancellation]() mutable {
        WorkerResult result;
        const auto fetch = [&](std::string_view url, std::string& error) {
            if (config.fetch)
                return config.fetch(url, error);
            http::Request request;
            request.url = std::string(url);
            request.user_agent = "Draxul SatView";
            request.connect_timeout = std::chrono::seconds(10);
            request.overall_timeout = std::chrono::seconds(60);
            request.max_response_bytes = kMaxCatalogResponseBytes;
            auto response = config.http_client->get(request, cancellation);
            const bool succeeded = response.ok();
            if (!succeeded)
                error = std::move(response.error);
            return succeeded ? std::move(response.body) : std::string{};
        };
        if (refresh_gp)
        {
            result.gp.attempted = true;
            result.gp.fetched_at = Clock::now();
            const std::string gp_url = default_celestrak_url(config.celestrak_group);
            result.gp.raw_payload = fetch(gp_url, result.gp.error);
            if (result.gp.raw_payload.empty())
            {
                if (result.gp.error.empty())
                    result.gp.error = "empty CelesTrak GP response";
            }
            else
            {
                auto parsed = parse_celestrak_gp_json(result.gp.raw_payload, config.celestrak_group, gp_url);
                if (!parsed)
                {
                    result.gp.error = parsed.error;
                }
                else
                {
                    result.gp.success = true;
                    result.gp.catalog = std::move(parsed.catalog);
                    gp_catalog = result.gp.catalog;
                    std::string cache_error;
                    if (!write_cache_files(
                            cache_json_path(config.cache_directory, config.celestrak_group),
                            cache_metadata_path(config.cache_directory, config.celestrak_group),
                            result.gp.raw_payload,
                            result.gp.catalog,
                            result.gp.fetched_at,
                            cache_error))
                    {
                        DRAXUL_LOG_WARN(LogCategory::Renderer,
                            "SatView: failed to write GP cache: %s",
                            cache_error.c_str());
                    }
                }
            }
        }

        if (refresh_satcat)
        {
            result.satcat.attempted = true;
            result.satcat.fetched_at = Clock::now();
            result.satcat.raw_payload = fetch(config.satcat_url, result.satcat.error);
            if (result.satcat.raw_payload.empty())
            {
                if (result.satcat.error.empty())
                    result.satcat.error = "empty CelesTrak SATCAT response";
            }
            else
            {
                auto parsed = parse_celestrak_satcat_csv(
                    result.satcat.raw_payload,
                    "SATCAT",
                    config.satcat_url);
                if (!parsed)
                {
                    result.satcat.error = parsed.error;
                }
                else
                {
                    result.satcat.success = true;
                    result.satcat.catalog = std::move(parsed.catalog);
                    std::string disposition_error;
                    const std::size_t disposition_count =
                        load_bundled_lunar_dispositions(
                            result.satcat.catalog,
                            &disposition_error);
                    if (disposition_count > 0)
                        DRAXUL_LOG_DEBUG(LogCategory::Renderer,
                            "SatView: excluded %zu confirmed non-orbiting lunar objects",
                            disposition_count);
                    if (!disposition_error.empty())
                        DRAXUL_LOG_WARN(LogCategory::Renderer,
                            "SatView: %s",
                            disposition_error.c_str());
                    satcat_catalog = result.satcat.catalog;
                    std::string cache_error;
                    if (!write_cache_files(
                            satcat_csv_path(config.cache_directory),
                            satcat_metadata_path(config.cache_directory),
                            result.satcat.raw_payload,
                            result.satcat.catalog,
                            result.satcat.fetched_at,
                            cache_error))
                    {
                        DRAXUL_LOG_WARN(LogCategory::Renderer,
                            "SatView: failed to write SATCAT cache: %s",
                            cache_error.c_str());
                    }
                }
            }
        }

        result.catalog_changed = result.gp.success || result.satcat.success;
        if (result.catalog_changed)
        {
            result.merged_catalog = merge_satellite_catalogs(gp_catalog, satcat_catalog);
            std::string ephemeris_error;
            const std::size_t ephemeris_count =
                load_bundled_sampled_ephemeris(result.merged_catalog, &ephemeris_error);
            if (ephemeris_count > 0)
                DRAXUL_LOG_DEBUG(LogCategory::Renderer,
                    "SatView: applied sampled ephemerides to %zu lunar objects", ephemeris_count);
            if (!ephemeris_error.empty())
                DRAXUL_LOG_WARN(LogCategory::Renderer, "SatView: %s", ephemeris_error.c_str());
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
    const auto apply_source = [](SourceStatus& status, const WorkerResult::SourceResult& source) {
        if (!source.attempted)
            return;
        if (source.success)
        {
            status.data_source = DataSource::Live;
            status.refresh_state = RefreshState::Idle;
            status.object_count = source.catalog.objects.size();
            status.malformed_records = source.catalog.malformed_records;
            status.excluded_records = source.catalog.excluded_records;
            status.non_renderable_records = source.catalog.non_renderable_records;
            status.fetched_at = source.fetched_at;
            status.cache_age = std::chrono::seconds::zero();
            status.source_label = source.catalog.source_label;
            status.source_url = source.catalog.source_url;
            status.error.clear();
        }
        else
        {
            status.refresh_state = RefreshState::Failed;
            status.error = source.error;
        }
    };

    apply_source(status_.gp, result.gp);
    apply_source(status_.satcat, result.satcat);
    if (result.gp.success)
        gp_catalog_ = result.gp.catalog;
    if (result.satcat.success)
        satcat_catalog_ = result.satcat.catalog;

    if (result.catalog_changed)
    {
        catalog_ = std::move(result.merged_catalog);
        ++catalog_generation_;
    }

    const bool failed = (result.gp.attempted && !result.gp.success)
        || (result.satcat.attempted && !result.satcat.success);
    status_.refresh_state = failed ? RefreshState::Failed : RefreshState::Idle;
    status_.error.clear();
    if (result.gp.attempted && !result.gp.success)
        status_.error = "GP: " + result.gp.error;
    if (result.satcat.attempted && !result.satcat.success)
    {
        if (!status_.error.empty())
            status_.error += " | ";
        status_.error += "SATCAT: " + result.satcat.error;
    }
    if (failed)
        DRAXUL_LOG_WARN(LogCategory::Renderer, "SatView: catalog refresh incomplete: %s", status_.error.c_str());
    publish_status_locked();
}

void SatViewCatalogService::publish_status_locked()
{
    const auto now = Clock::now();
    const auto update_age = [&](SourceStatus& source) {
        if ((source.data_source == DataSource::Cache || source.data_source == DataSource::Live)
            && source.fetched_at != Clock::time_point{})
        {
            source.cache_age = std::chrono::duration_cast<std::chrono::seconds>(now - source.fetched_at);
        }
    };
    update_age(status_.gp);
    update_age(status_.satcat);

    const auto source_text = [](std::string_view name, const SourceStatus& source) {
        std::string text(name);
        text += ": ";
        switch (source.data_source)
        {
        case DataSource::Live:
            text += "live";
            break;
        case DataSource::Cache:
            text += "cached";
            break;
        case DataSource::Sample:
            text += "sample";
            break;
        case DataSource::None:
            text += "pending";
            break;
        }
        if (source.data_source != DataSource::None)
            text += " " + std::to_string(source.object_count);
        if (source.data_source == DataSource::Cache)
            text += " " + format_age(source.cache_age) + " old";
        if (source.refresh_state == RefreshState::Loading)
            text += " loading";
        else if (source.refresh_state == RefreshState::Refreshing)
            text += " refreshing";
        else if (source.refresh_state == RefreshState::Failed)
            text += " failed";
        return text;
    };

    status_.object_count = catalog_.objects.size();
    status_.renderable_count = renderable_satellite_count(catalog_);
    status_.non_renderable_count = catalog_.non_renderable_records;
    status_.skipped_records = catalog_.skipped_records + catalog_.non_renderable_records;
    status_.populations = satellite_population_counts(catalog_);
    status_.source_label = catalog_.source_label;
    status_.source_url = catalog_.source_url;

    if (status_.gp.data_source != DataSource::None)
    {
        status_.data_source = status_.gp.data_source;
        status_.fetched_at = status_.gp.fetched_at;
        status_.cache_age = status_.gp.cache_age;
    }
    else if (status_.satcat.data_source != DataSource::None)
    {
        status_.data_source = status_.satcat.data_source;
        status_.fetched_at = status_.satcat.fetched_at;
        status_.cache_age = status_.satcat.cache_age;
    }
    else if (catalog_.source_label == "sample")
    {
        status_.data_source = DataSource::Sample;
    }
    else
    {
        status_.data_source = DataSource::None;
    }

    if (status_.data_source == DataSource::Sample)
    {
        status_.text = "sample " + std::to_string(status_.object_count) + " sats";
    }
    else
    {
        status_.text = source_text("GP", status_.gp) + " | " + source_text("SATCAT", status_.satcat);
        status_.text += " | merged " + std::to_string(status_.object_count)
            + " (" + std::to_string(status_.renderable_count) + " renderable)";
    }
}

bool SatViewCatalogService::has_fresh_source_locked(
    const SourceStatus& source,
    std::chrono::seconds refresh_interval,
    Clock::time_point now) const
{
    if (source.fetched_at == Clock::time_point{})
        return false;
    if (source.data_source != DataSource::Cache && source.data_source != DataSource::Live)
        return false;
    const auto age = std::chrono::duration_cast<std::chrono::seconds>(now - source.fetched_at);
    return age < refresh_interval;
}

} // namespace draxul::satview
