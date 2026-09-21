#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace draxul::weather::detail
{

struct Coordinates
{
    double latitude = 0.0;
    double longitude = 0.0;
};

struct GeocodingQuery
{
    std::string city_name;
    std::string country_filter;
};

struct CurrentWeather
{
    double temperature_c = 0.0;
    int weather_code = 0;
};

// These synchronous helpers are private implementation details surfaced only
// through the test-internals target. Production callers use WeatherService.
[[nodiscard]] std::optional<Coordinates> parse_coordinate_location(std::string_view location);
[[nodiscard]] std::optional<GeocodingQuery> parse_geocoding_query(std::string_view location);
[[nodiscard]] std::optional<Coordinates> decode_geocoding_response(
    std::string_view body, std::string_view country_filter);
[[nodiscard]] std::optional<CurrentWeather> decode_current_weather_response(
    std::string_view body);
[[nodiscard]] std::string weather_emoji(int code);
[[nodiscard]] std::string format_temperature(double temperature_c);

} // namespace draxul::weather::detail
