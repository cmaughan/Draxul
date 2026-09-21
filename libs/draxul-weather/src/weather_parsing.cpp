#include "weather_parsing.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <optional>
#include <utility>

namespace draxul::weather::detail
{
namespace
{

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

std::optional<Coordinates> parse_coordinate_location(std::string_view location)
{
    const auto comma = location.find(',');
    if (comma == std::string_view::npos)
        return std::nullopt;

    const std::string latitude_text = trim_ascii(std::string(location.substr(0, comma)));
    const std::string longitude_text = trim_ascii(std::string(location.substr(comma + 1)));
    if (latitude_text.empty() || longitude_text.empty())
        return std::nullopt;

    char* latitude_end = nullptr;
    char* longitude_end = nullptr;
    const double latitude = std::strtod(latitude_text.c_str(), &latitude_end);
    const double longitude = std::strtod(longitude_text.c_str(), &longitude_end);
    if (latitude_end != latitude_text.c_str() + latitude_text.size()
        || longitude_end != longitude_text.c_str() + longitude_text.size()
        || !std::isfinite(latitude) || !std::isfinite(longitude)
        || latitude < -90.0 || latitude > 90.0
        || longitude < -180.0 || longitude > 180.0)
    {
        return std::nullopt;
    }

    return Coordinates{ latitude, longitude };
}

std::optional<GeocodingQuery> parse_geocoding_query(std::string_view location)
{
    std::string city_name(location);
    std::string country_filter;
    if (const auto comma = city_name.find(','); comma != std::string::npos)
    {
        country_filter = trim_ascii(city_name.substr(comma + 1));
        city_name.resize(comma);
    }
    city_name = trim_ascii(std::move(city_name));
    if (city_name.empty())
        return std::nullopt;
    return GeocodingQuery{ std::move(city_name), std::move(country_filter) };
}

std::optional<Coordinates> decode_geocoding_response(
    std::string_view body, std::string_view country_filter)
{
    const nlohmann::json document = nlohmann::json::parse(body, nullptr, false);
    if (document.is_discarded() || !document.is_object())
        return std::nullopt;
    const auto results_it = document.find("results");
    if (results_it == document.end() || !results_it->is_array())
        return std::nullopt;

    std::optional<Coordinates> first_valid;
    for (const auto& entry : *results_it)
    {
        if (!entry.is_object())
            continue;
        double latitude = 0.0;
        double longitude = 0.0;
        if (!json_finite_number(entry, "latitude", latitude)
            || !json_finite_number(entry, "longitude", longitude)
            || latitude < -90.0 || latitude > 90.0
            || longitude < -180.0 || longitude > 180.0)
        {
            continue;
        }

        const Coordinates coordinates{ latitude, longitude };
        if (!first_valid)
            first_valid = coordinates;

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
            return coordinates;
        }
    }

    // Preserve the provider behavior: a country hint improves selection but
    // does not turn an otherwise usable geocoding response into a failure.
    return first_valid;
}

std::optional<CurrentWeather> decode_current_weather_response(std::string_view body)
{
    const nlohmann::json document = nlohmann::json::parse(body, nullptr, false);
    if (document.is_discarded() || !document.is_object())
        return std::nullopt;
    const auto current = document.find("current_weather");
    if (current == document.end() || !current->is_object())
        return std::nullopt;

    double temperature_c = 0.0;
    double code = 0.0;
    if (!json_finite_number(*current, "temperature", temperature_c)
        || !json_finite_number(*current, "weathercode", code)
        || temperature_c < -100.0 || temperature_c > 100.0
        || code < 0.0 || code > 99.0 || std::floor(code) != code)
    {
        return std::nullopt;
    }
    return CurrentWeather{ temperature_c, static_cast<int>(code) };
}

std::string weather_emoji(int code)
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

std::string format_temperature(double temperature_c)
{
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.0f°C", temperature_c);
    return buffer;
}

} // namespace draxul::weather::detail
