#include "weather_service.h"

#include <draxul/log.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <limits>
#include <optional>

namespace draxul
{
namespace
{

constexpr std::size_t kMaxWeatherResponseBytes = 1024 * 1024;

void append_utf8_codepoint(std::string& out, uint32_t codepoint)
{
    if (codepoint <= 0x7F)
        out.push_back(static_cast<char>(codepoint));
    else if (codepoint <= 0x7FF)
    {
        out.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
    else if (codepoint <= 0xFFFF)
    {
        out.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
    else
    {
        out.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
}

std::string utf8_from_codepoints(std::initializer_list<uint32_t> codepoints)
{
    std::string out;
    out.reserve(codepoints.size() * 4);
    for (uint32_t codepoint : codepoints)
        append_utf8_codepoint(out, codepoint);
    return out;
}

std::string trim_ascii(std::string value)
{
    const auto whitespace = [](unsigned char c) { return std::isspace(c) != 0; };
    value.erase(value.begin(), std::find_if_not(value.begin(), value.end(), whitespace));
    value.erase(std::find_if_not(value.rbegin(), value.rend(), whitespace).base(), value.end());
    return value;
}

bool contains_ascii_case_insensitive(std::string_view haystack, std::string_view needle)
{
    if (needle.empty())
        return true;
    return std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
               [](unsigned char a, unsigned char b) { return std::tolower(a) == std::tolower(b); })
        != haystack.end();
}

bool json_finite_number(const nlohmann::json& object, const char* key, double& value)
{
    const auto it = object.find(key);
    if (it == object.end() || !it->is_number())
        return false;
    value = it->get<double>();
    return std::isfinite(value);
}

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
    double lat = 0.0;
    double lon = 0.0;
    bool have_coords = false;
    if (const auto comma = location.find(','); comma != std::string::npos)
    {
        const std::string latitude_text = trim_ascii(location.substr(0, comma));
        const std::string longitude_text = trim_ascii(location.substr(comma + 1));
        char* latitude_end = nullptr;
        char* longitude_end = nullptr;
        const double parsed_latitude = std::strtod(latitude_text.c_str(), &latitude_end);
        const double parsed_longitude = std::strtod(longitude_text.c_str(), &longitude_end);
        have_coords = !latitude_text.empty() && !longitude_text.empty()
            && latitude_end == latitude_text.c_str() + latitude_text.size()
            && longitude_end == longitude_text.c_str() + longitude_text.size()
            && std::isfinite(parsed_latitude) && std::isfinite(parsed_longitude)
            && parsed_latitude >= -90.0 && parsed_latitude <= 90.0
            && parsed_longitude >= -180.0 && parsed_longitude <= 180.0;
        if (have_coords)
        {
            lat = parsed_latitude;
            lon = parsed_longitude;
        }
    }

    if (!have_coords && !try_geocode(location, lat, lon))
    {
        DRAXUL_LOG_DEBUG(LogCategory::App,
            "WeatherService: geocode failed for '%s'; disabling", location.c_str());
        running_ = false;
        return;
    }

    DRAXUL_LOG_DEBUG(LogCategory::App,
        "WeatherService: resolved location to %.4f,%.4f", lat, lon);

    constexpr auto kFetchInterval = std::chrono::minutes(10);
    constexpr auto kSleepGranularity = std::chrono::milliseconds(100);
    while (running_)
    {
        double temp_c = 0.0;
        int weather_code = 0;
        if (fetch_temperature(lat, lon, temp_c, weather_code))
        {
            char temp_buf[32];
            std::snprintf(temp_buf, sizeof(temp_buf), "%.0f°C", temp_c);
            {
                std::lock_guard lock(mutex_);
                emoji_ = weather_emoji(weather_code);
                temperature_ = temp_buf;
            }
            has_data_ = true;
        }

        const auto deadline = std::chrono::steady_clock::now() + kFetchInterval;
        while (running_ && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(kSleepGranularity);
    }
}

bool WeatherService::try_geocode(const std::string& query, double& lat, double& lon)
{
    std::string city_name = query;
    std::string country_filter;
    if (const auto comma = city_name.find(','); comma != std::string::npos)
    {
        country_filter = trim_ascii(city_name.substr(comma + 1));
        city_name.resize(comma);
    }
    city_name = trim_ascii(std::move(city_name));
    if (city_name.empty())
        return false;

    const std::string url = "https://geocoding-api.open-meteo.com/v1/search?name="
        + http::encode_query_component(city_name)
        + "&count=10&language=en&format=json";
    const std::string body = get(url);
    if (body.empty())
        return false;

    const nlohmann::json document = nlohmann::json::parse(body, nullptr, false);
    if (document.is_discarded() || !document.is_object())
        return false;
    const auto results_it = document.find("results");
    if (results_it == document.end() || !results_it->is_array())
        return false;

    std::optional<std::pair<double, double>> first_valid;
    for (const auto& entry : *results_it)
    {
        if (!entry.is_object())
            continue;
        double entry_latitude = 0.0;
        double entry_longitude = 0.0;
        if (!json_finite_number(entry, "latitude", entry_latitude)
            || !json_finite_number(entry, "longitude", entry_longitude)
            || entry_latitude < -90.0 || entry_latitude > 90.0
            || entry_longitude < -180.0 || entry_longitude > 180.0)
        {
            continue;
        }
        if (!first_valid)
            first_valid = std::pair(entry_latitude, entry_longitude);

        std::string country;
        std::string country_code;
        if (const auto it = entry.find("country"); it != entry.end() && it->is_string())
            country = it->get<std::string>();
        if (const auto it = entry.find("country_code"); it != entry.end() && it->is_string())
            country_code = it->get<std::string>();
        if (country_filter.empty()
            || contains_ascii_case_insensitive(country, country_filter)
            || contains_ascii_case_insensitive(country_code, country_filter))
        {
            lat = entry_latitude;
            lon = entry_longitude;
            return true;
        }
    }

    if (!first_valid)
        return false;
    lat = first_valid->first;
    lon = first_valid->second;
    return true;
}

bool WeatherService::fetch_temperature(double lat, double lon, double& temp_c, int& weather_code)
{
    char url[256];
    std::snprintf(url, sizeof(url),
        "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f&current_weather=true",
        lat, lon);
    const std::string body = get(url);
    if (body.empty())
        return false;

    const nlohmann::json document = nlohmann::json::parse(body, nullptr, false);
    if (document.is_discarded() || !document.is_object())
        return false;
    const auto current = document.find("current_weather");
    if (current == document.end() || !current->is_object())
        return false;

    double code = 0.0;
    if (!json_finite_number(*current, "temperature", temp_c)
        || !json_finite_number(*current, "weathercode", code)
        || temp_c < -100.0 || temp_c > 100.0
        || code < 0.0 || code > 99.0 || std::floor(code) != code)
    {
        return false;
    }
    weather_code = static_cast<int>(code);
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

std::string WeatherService::weather_emoji(int code)
{
    static const std::string kClear = utf8_from_codepoints({ 0x2600, 0xFE0F });
    static const std::string kPartlyCloudy = utf8_from_codepoints({ 0x26C5 });
    static const std::string kCloud = utf8_from_codepoints({ 0x2601, 0xFE0F });
    static const std::string kRain = utf8_from_codepoints({ 0x1F327, 0xFE0F });
    static const std::string kSnow = utf8_from_codepoints({ 0x2744, 0xFE0F });
    static const std::string kThunder = utf8_from_codepoints({ 0x26A1 });
    static const std::string kFallback = utf8_from_codepoints({ 0x1F321, 0xFE0F });
    if (code == 0)
        return kClear;
    if (code <= 3)
        return kPartlyCloudy;
    if (code <= 48)
        return kCloud;
    if (code <= 67)
        return kRain;
    if (code <= 77)
        return kSnow;
    if (code <= 82)
        return kRain;
    if (code <= 86)
        return kSnow;
    if (code <= 99)
        return kThunder;
    return kFallback;
}

} // namespace draxul
