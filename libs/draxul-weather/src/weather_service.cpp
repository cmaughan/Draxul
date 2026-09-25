#include <draxul/weather_service.h>

#include "weather_parsing.h"

#include <draxul/log.h>

#include <chrono>
#include <cstdio>
#include <utility>

namespace draxul
{
namespace
{

constexpr std::size_t kMaxWeatherResponseBytes = 1024 * 1024;
constexpr auto kFetchInterval = std::chrono::minutes(10);
constexpr auto kSleepGranularity = std::chrono::milliseconds(100);

} // namespace

WeatherService::WeatherService()
    : WeatherService(http::create_platform_http_client())
{
}

WeatherService::WeatherService(std::shared_ptr<http::IHttpClient> http_client)
    : http_client_(std::move(http_client))
{
}

WeatherService::~WeatherService()
{
    stop();
}

void WeatherService::start(const std::string& location)
{
    if (location.empty())
        return;
    if (running_.exchange(true))
        return;
    if (thread_.joinable())
        thread_.join();
    cancellation_ = http::CancellationSource{};
    thread_ = std::thread(&WeatherService::worker_func, this, location);
}

void WeatherService::stop()
{
    running_ = false;
    cancellation_.cancel();
    if (thread_.joinable())
        thread_.join();
    // A cancelled request may have completed just before join. Clear only
    // after the worker is gone so an old location cannot republish its pill.
    {
        std::lock_guard lock(mutex_);
        emoji_.clear();
        temperature_.clear();
        has_data_ = false;
    }
}

void WeatherService::set_http_client(std::shared_ptr<http::IHttpClient> http_client)
{
    stop();
    http_client_ = std::move(http_client);
}

std::string WeatherService::display_text() const
{
    std::lock_guard lock(mutex_);
    if (emoji_.empty() && temperature_.empty())
        return {};
    return emoji_ + " " + temperature_;
}

std::string WeatherService::emoji() const
{
    std::lock_guard lock(mutex_);
    return emoji_;
}

std::string WeatherService::temperature() const
{
    std::lock_guard lock(mutex_);
    return temperature_;
}

void WeatherService::worker_func(std::string location)
{
    double latitude = 0.0;
    double longitude = 0.0;
    if (const auto coordinates = weather::detail::parse_coordinate_location(location))
    {
        latitude = coordinates->latitude;
        longitude = coordinates->longitude;
    }
    else if (!try_geocode(location, latitude, longitude))
    {
        DRAXUL_LOG_DEBUG(LogCategory::App,
            "WeatherService: geocode failed for '%s'; disabling", location.c_str());
        running_ = false;
        return;
    }

    DRAXUL_LOG_DEBUG(LogCategory::App,
        "WeatherService: resolved location to %.4f,%.4f", latitude, longitude);

    while (running_)
    {
        double temperature_c = 0.0;
        int weather_code = 0;
        if (fetch_temperature(latitude, longitude, temperature_c, weather_code)
            && running_)
        {
            {
                std::lock_guard lock(mutex_);
                if (running_)
                {
                    emoji_ = weather::detail::weather_emoji(weather_code);
                    temperature_ = weather::detail::format_temperature(temperature_c);
                    has_data_ = true;
                }
            }
        }

        const auto deadline = std::chrono::steady_clock::now() + kFetchInterval;
        while (running_ && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(kSleepGranularity);
    }
}

bool WeatherService::try_geocode(const std::string& query, double& latitude, double& longitude)
{
    const auto parsed_query = weather::detail::parse_geocoding_query(query);
    if (!parsed_query)
        return false;

    const std::string url = "https://geocoding-api.open-meteo.com/v1/search?name="
        + http::encode_query_component(parsed_query->city_name)
        + "&count=10&language=en&format=json";
    const std::string body = get(url);
    if (body.empty())
        return false;

    const auto coordinates = weather::detail::decode_geocoding_response(
        body, parsed_query->country_filter);
    if (!coordinates)
        return false;
    latitude = coordinates->latitude;
    longitude = coordinates->longitude;
    return true;
}

bool WeatherService::fetch_temperature(
    double latitude, double longitude, double& temperature_c, int& weather_code)
{
    char url[256];
    std::snprintf(url, sizeof(url),
        "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f&current_weather=true",
        latitude, longitude);
    const std::string body = get(url);
    if (body.empty())
        return false;

    const auto current = weather::detail::decode_current_weather_response(body);
    if (!current)
        return false;
    temperature_c = current->temperature_c;
    weather_code = current->weather_code;
    return true;
}

std::string WeatherService::get(std::string url)
{
    if (!http_client_)
        return {};
    http::Request request;
    request.url = std::move(url);
    request.user_agent = "Draxul Weather";
    request.connect_timeout = std::chrono::seconds(10);
    request.overall_timeout = std::chrono::seconds(15);
    request.max_response_bytes = kMaxWeatherResponseBytes;
    auto response = http_client_->get(request, cancellation_.token());
    return response.ok() ? std::move(response.body) : std::string{};
}

} // namespace draxul
