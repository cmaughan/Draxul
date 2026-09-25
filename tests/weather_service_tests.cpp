#include <draxul/weather_service.h>
#include <draxul/chrome_layout.h>

#include "weather_parsing.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <draxul/http/http_client.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace
{

class FakeHttpClient final : public draxul::http::IHttpClient
{
public:
    using Handler = std::function<draxul::http::Response(
        const draxul::http::Request&, draxul::http::CancellationToken)>;

    explicit FakeHttpClient(Handler handler)
        : handler_(std::move(handler))
    {
    }

    draxul::http::Response get(
        const draxul::http::Request& request,
        draxul::http::CancellationToken cancellation) override
    {
        {
            std::lock_guard lock(mutex_);
            requests_.push_back(request);
        }
        return handler_(request, cancellation);
    }

    [[nodiscard]] std::vector<draxul::http::Request> requests() const
    {
        std::lock_guard lock(mutex_);
        return requests_;
    }

private:
    Handler handler_;
    mutable std::mutex mutex_;
    std::vector<draxul::http::Request> requests_;
};

draxul::http::Response success(std::string body)
{
    draxul::http::Response response;
    response.status_code = 200;
    response.body = std::move(body);
    return response;
}

bool wait_for_weather(draxul::WeatherService& weather)
{
    return draxul::tests::wait_until(
        [&] { return weather.has_data(); },
        std::chrono::milliseconds(500), std::chrono::milliseconds(5));
}

bool wait_for_requests(const FakeHttpClient& client, std::size_t count)
{
    return draxul::tests::wait_until(
        [&] { return client.requests().size() >= count; },
        std::chrono::milliseconds(500), std::chrono::milliseconds(2));
}

} // namespace

TEST_CASE("weather coordinate parsing is synchronous and range checked", "[weather][parsing]")
{
    using draxul::weather::detail::parse_coordinate_location;

    const auto ordinary = parse_coordinate_location(" 53.96 , -1.08 ");
    REQUIRE(ordinary);
    CHECK(ordinary->latitude == Catch::Approx(53.96));
    CHECK(ordinary->longitude == Catch::Approx(-1.08));

    const auto limits = parse_coordinate_location("-90,180");
    REQUIRE(limits);
    CHECK(limits->latitude == -90.0);
    CHECK(limits->longitude == 180.0);

    for (std::string_view invalid : {
             "York", "", ",", "1,", ",2", "91,0", "-91,0", "0,181", "0,-181",
             "nan,0", "0,inf", "1,2,3", "1x,2", "1,2x" })
    {
        CAPTURE(invalid);
        CHECK_FALSE(parse_coordinate_location(invalid));
    }
}

TEST_CASE("weather geocoding query parsing preserves city and optional country hint",
    "[weather][parsing]")
{
    using draxul::weather::detail::parse_geocoding_query;

    const auto city_and_country = parse_geocoding_query("  York  ,  GB  ");
    REQUIRE(city_and_country);
    CHECK(city_and_country->city_name == "York");
    CHECK(city_and_country->country_filter == "GB");

    const auto city_only = parse_geocoding_query("  New York  ");
    REQUIRE(city_only);
    CHECK(city_only->city_name == "New York");
    CHECK(city_only->country_filter.empty());

    const auto first_comma = parse_geocoding_query("One, Two, Three");
    REQUIRE(first_comma);
    CHECK(first_comma->city_name == "One");
    CHECK(first_comma->country_filter == "Two, Three");

    CHECK_FALSE(parse_geocoding_query("  , GB"));
}

TEST_CASE("weather geocoding decoding selects country matches then falls back to first valid result",
    "[weather][provider]")
{
    using draxul::weather::detail::decode_geocoding_response;
    constexpr std::string_view body = R"({"results":[
        {"latitude":"bad","longitude":20,"country":"Ignored"},
        {"latitude":43.65,"longitude":-79.38,"country":"Canada","country_code":"CA"},
        {"latitude":53.96,"longitude":-1.08,"country":"United Kingdom","country_code":"GB"}
    ]})";

    const auto matched = decode_geocoding_response(body, "kingDOM");
    REQUIRE(matched);
    CHECK(matched->latitude == Catch::Approx(53.96));
    CHECK(matched->longitude == Catch::Approx(-1.08));

    const auto matched_code = decode_geocoding_response(body, "gb");
    REQUIRE(matched_code);
    CHECK(matched_code->latitude == Catch::Approx(53.96));

    const auto fallback = decode_geocoding_response(body, "missing country");
    REQUIRE(fallback);
    CHECK(fallback->latitude == Catch::Approx(43.65));
    CHECK(fallback->longitude == Catch::Approx(-79.38));
}

TEST_CASE("weather geocoding decoding rejects malformed shapes and coordinates",
    "[weather][provider]")
{
    using draxul::weather::detail::decode_geocoding_response;
    for (std::string_view body : {
             R"({"results":[{"longitude":-1.0}]})",
             R"({"results":[{"latitude":"53","longitude":-1.0}]})",
             R"({"results":[{"latitude":91,"longitude":-1.0}]})",
             R"({"results":[{"latitude":53,"longitude":181}]})",
             R"({"results":{}})",
             R"({"results":[{"latitude":1e309,"longitude":0}]})",
             R"({"results":[]})",
             R"([])",
             "not json" })
    {
        CAPTURE(body);
        CHECK_FALSE(decode_geocoding_response(body, {}));
    }
}

TEST_CASE("weather current response decoding validates types and provider ranges",
    "[weather][provider]")
{
    using draxul::weather::detail::decode_current_weather_response;
    const auto valid = decode_current_weather_response(
        R"({"current_weather":{"temperature":18.4,"weathercode":2}})");
    REQUIRE(valid);
    CHECK(valid->temperature_c == Catch::Approx(18.4));
    CHECK(valid->weather_code == 2);

    for (std::string_view body : {
             R"({"current_weather":{"temperature":12}})",
             R"({"current_weather":{"temperature":"12","weathercode":1}})",
             R"({"current_weather":{"temperature":101,"weathercode":1}})",
             R"({"current_weather":{"temperature":-101,"weathercode":1}})",
             R"({"current_weather":{"temperature":12,"weathercode":1.5}})",
             R"({"current_weather":{"temperature":12,"weathercode":100}})",
             R"({"current_weather":{"temperature":1e309,"weathercode":1}})",
             R"({"current_weather":[]})",
             R"({})",
             "not json" })
    {
        CAPTURE(body);
        CHECK_FALSE(decode_current_weather_response(body));
    }
}

TEST_CASE("weather presentation keeps existing emoji bands and Celsius rounding",
    "[weather][formatting]")
{
    using draxul::weather::detail::format_temperature;
    using draxul::weather::detail::weather_emoji;

    CHECK(weather_emoji(0) == "☀️");
    CHECK(weather_emoji(2) == "⛅");
    CHECK(weather_emoji(45) == "☁️");
    CHECK(weather_emoji(61) == "\xF0\x9F\x8C\xA7\xEF\xB8\x8F");
    CHECK(weather_emoji(71) == "❄️");
    CHECK(weather_emoji(95) == "⚡");
    CHECK(weather_emoji(100) == "\xF0\x9F\x8C\xA1\xEF\xB8\x8F");
    CHECK(format_temperature(18.4) == "18°C");
    CHECK(format_temperature(-2.6) == "-3°C");
}

TEST_CASE("weather encodes geocoding data and publishes provider output",
    "[weather][transport]")
{
    auto client = std::make_shared<FakeHttpClient>([](const auto& request, auto) {
        if (request.url.find("geocoding-api") != std::string::npos)
        {
            return success(R"({"results":[{"latitude":53.96,"longitude":-1.08,"country":"United Kingdom","country_code":"GB"}]})");
        }
        return success(R"({"current_weather":{"temperature":18.4,"weathercode":2}})");
    });
    draxul::WeatherService weather(client);
    const std::string city = "York \"&|;#%/?+ \xC3\x9C";
    weather.start(city + ", Kingdom");
    REQUIRE(wait_for_weather(weather));
    CHECK(weather.emoji() == "⛅");
    CHECK(weather.temperature() == "18°C");
    CHECK(weather.display_text() == "⛅ 18°C");
    weather.stop();

    const auto requests = client->requests();
    REQUIRE(requests.size() == 2);
    CHECK(requests[0].url.find("name=" + draxul::http::encode_query_component(city))
        != std::string::npos);
    CHECK(requests[0].url.find('"') == std::string::npos);
    CHECK(requests[0].url.find('|') == std::string::npos);
    CHECK(requests[1].url.find("latitude=53.9600") != std::string::npos);
    CHECK(requests[1].url.find("longitude=-1.0800") != std::string::npos);
    CHECK(weather.emoji().empty());
    CHECK(weather.temperature().empty());
    CHECK(weather.display_text().empty());
    CHECK_FALSE(weather.has_data());
}

TEST_CASE("weather location switch clears old presentation until fresh data arrives",
    "[weather][transport]")
{
    std::atomic<bool> release_second = false;
    std::atomic<bool> second_entered = false;
    auto client = std::make_shared<FakeHttpClient>([&](const auto& request, auto cancellation) {
        if (request.url.find("latitude=40.7000") != std::string::npos)
        {
            second_entered = true;
            while (!release_second && !cancellation.is_cancelled())
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            return success(R"({"current_weather":{"temperature":7.2,"weathercode":61}})");
        }
        return success(R"({"current_weather":{"temperature":18.4,"weathercode":2}})");
    });
    draxul::WeatherService weather(client);
    weather.start("51.5,-0.1");
    REQUIRE(wait_for_weather(weather));
    CHECK(weather.temperature() == "18°C");

    weather.stop();
    CHECK_FALSE(weather.has_data());
    CHECK(weather.emoji().empty());
    CHECK(weather.temperature().empty());
    CHECK(weather.display_text().empty());

    weather.start("40.7,-74.0");
    REQUIRE(draxul::tests::wait_until([&] { return second_entered.load(); },
        std::chrono::milliseconds(500), std::chrono::milliseconds(2)));
    CHECK_FALSE(weather.has_data());
    CHECK(weather.display_text().empty());
    release_second = true;
    REQUIRE(wait_for_weather(weather));
    CHECK(weather.temperature() == "7°C");
    CHECK(weather.emoji() == "\xF0\x9F\x8C\xA7\xEF\xB8\x8F");
    weather.stop();
    CHECK(weather.display_text().empty());
}

TEST_CASE("weather updates the chrome pill without retaining a previous location",
    "[weather][chrome][integration]")
{
    std::atomic<bool> release_second = false;
    std::atomic<bool> second_entered = false;
    auto client = std::make_shared<FakeHttpClient>([&](const auto& request, auto cancellation) {
        if (request.url.find("latitude=40.7000") != std::string::npos)
        {
            second_entered = true;
            while (!release_second && !cancellation.is_cancelled())
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            return success(R"({"current_weather":{"temperature":7.2,"weathercode":61}})");
        }
        return success(R"({"current_weather":{"temperature":18.4,"weathercode":2}})");
    });
    draxul::WeatherService weather(client);
    draxul::ChromeLayoutInput input;
    input.viewport_width = 800;
    input.viewport_height = 600;
    input.cell_width = 10;
    input.cell_height = 20;
    input.show_top_bar = true;
    input.tabs = { { 1, "weather", true } };
    input.shell_layout = draxul::compute_app_shell_layout({
        .window_width = 800,
        .window_height = 600,
        .terminal_height = 600,
        .cell_width = 10,
        .cell_height = 20,
    });
    const auto pill_text = [&] {
        input.weather_emoji = weather.emoji();
        input.weather_temperature = weather.temperature();
        const auto layout = draxul::compute_chrome_layout(input);
        if (layout.right_pills.empty())
            return std::string{};
        std::string text;
        for (const auto& cluster : layout.right_pills.front().clusters)
            text += cluster.text;
        return text;
    };

    CHECK(pill_text().empty());
    weather.start("51.5,-0.1");
    REQUIRE(wait_for_weather(weather));
    CHECK(pill_text() == "⛅ 18°C");

    weather.stop(); // The same transition used by App::reload_config.
    CHECK(pill_text().empty());
    weather.start("40.7,-74.0");
    REQUIRE(draxul::tests::wait_until([&] { return second_entered.load(); },
        std::chrono::milliseconds(500), std::chrono::milliseconds(2)));
    CHECK(pill_text().empty());
    release_second = true;
    REQUIRE(wait_for_weather(weather));
    CHECK(pill_text() == "🌧️ 7°C");
    weather.stop();
}

TEST_CASE("weather requests preserve transport budgets", "[weather][transport]")
{
    auto client = std::make_shared<FakeHttpClient>([](const auto&, auto) {
        draxul::http::Response response;
        response.status_code = 200;
        response.error = "HTTP response exceeded byte limit";
        return response;
    });
    draxul::WeatherService weather(client);
    weather.start("51.5,-0.1");
    REQUIRE(wait_for_requests(*client, 1));
    weather.stop();

    const auto requests = client->requests();
    REQUIRE(requests.size() == 1);
    CHECK(requests[0].user_agent == "Draxul Weather");
    CHECK(requests[0].connect_timeout == std::chrono::seconds(10));
    CHECK(requests[0].overall_timeout == std::chrono::seconds(15));
    CHECK(requests[0].max_response_bytes == 1024 * 1024);
    CHECK_FALSE(weather.has_data());
}

TEST_CASE("empty weather locations issue no transport requests", "[weather][transport]")
{
    auto client = std::make_shared<FakeHttpClient>([](const auto&, auto) {
        return success("{}");
    });
    draxul::WeatherService weather(client);
    weather.start("");
    weather.stop();
    CHECK(client->requests().empty());
}

TEST_CASE("weather cancellation unblocks stop and the service can restart",
    "[weather][transport][cancellation]")
{
    std::atomic<int> calls = 0;
    std::atomic<bool> entered = false;
    auto client = std::make_shared<FakeHttpClient>([&](const auto&, auto cancellation) {
        if (++calls == 1)
        {
            entered = true;
            while (!cancellation.is_cancelled())
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            draxul::http::Response response;
            response.cancelled = true;
            response.error = "request cancelled";
            return response;
        }
        return success(R"({"current_weather":{"temperature":7.2,"weathercode":61}})");
    });
    draxul::WeatherService weather(client);
    weather.start("51.5,-0.1");
    REQUIRE(draxul::tests::wait_until(
        [&] { return entered.load(); },
        std::chrono::milliseconds(500), std::chrono::milliseconds(2)));

    const auto stop_started = std::chrono::steady_clock::now();
    weather.stop();
    CHECK(std::chrono::steady_clock::now() - stop_started
        < draxul::http::kCancellationShutdownBudget);
    CHECK_FALSE(weather.has_data());

    weather.start("40.7,-74.0");
    REQUIRE(wait_for_weather(weather));
    CHECK(weather.temperature() == "7°C");
    weather.stop();
    CHECK(calls == 2);
    CHECK(weather.temperature().empty());

    const auto requests = client->requests();
    REQUIRE(requests.size() == 2);
    CHECK(requests[1].url.find("latitude=40.7000") != std::string::npos);
    CHECK(requests[1].url.find("longitude=-74.0000") != std::string::npos);
}

TEST_CASE("HTTP query values use complete RFC 3986 encoding", "[http][weather]")
{
    CHECK(draxul::http::encode_query_component("AZaz09-._~") == "AZaz09-._~");
    CHECK(draxul::http::encode_query_component(" a&b=\"x\"#%/?+|")
        == "%20a%26b%3D%22x%22%23%25%2F%3F%2B%7C");
    CHECK(draxul::http::encode_query_component("\xC3\x9C") == "%C3%9C");
}
