#include "weather_service.h"

#include <catch2/catch_test_macros.hpp>
#include <draxul/http/http_client.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
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

    std::vector<draxul::http::Request> requests() const
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
        std::chrono::milliseconds(200), std::chrono::milliseconds(2));
}

} // namespace

TEST_CASE("HTTP query values use complete RFC 3986 encoding", "[http][weather]")
{
    CHECK(draxul::http::encode_query_component("AZaz09-._~") == "AZaz09-._~");
    CHECK(draxul::http::encode_query_component(" a&b=\"x\"#%/?+|")
        == "%20a%26b%3D%22x%22%23%25%2F%3F%2B%7C");
    CHECK(draxul::http::encode_query_component("\xC3\x9C") == "%C3%9C");
}

TEST_CASE("weather passes hostile and Unicode locations only as encoded URL data", "[http][weather]")
{
    auto client = std::make_shared<FakeHttpClient>([](const auto& request, auto) {
        if (request.url.find("geocoding-api") != std::string::npos)
        {
            return success(R"({"results":[{"latitude":53.96,"longitude":-1.08,"country":"United \u004b\u0069ngdom","country_code":"GB"}]})");
        }
        return success(R"({"current_weather":{"temperature":18.4,"weathercode":2}})");
    });
    draxul::WeatherService weather(client);
    const std::string city = "York \"&|;#%/?+ \xC3\x9C";
    weather.start(city + ", Kingdom");
    REQUIRE(wait_for_weather(weather));
    weather.stop();

    const auto requests = client->requests();
    REQUIRE(requests.size() == 2);
    CHECK(requests[0].url.find("name=" + draxul::http::encode_query_component(city)) != std::string::npos);
    CHECK(requests[0].url.find('"') == std::string::npos);
    CHECK(requests[0].url.find('|') == std::string::npos);
    CHECK(weather.temperature() == "18°C");
}

TEST_CASE("weather rejects malformed, missing, wrong-type, and out-of-range current data", "[http][weather]")
{
    const std::vector<std::string> invalid = {
        R"({"current_weather":{"temperature":12}})",
        R"({"current_weather":{"temperature":"12","weathercode":1}})",
        R"({"current_weather":{"temperature":101,"weathercode":1}})",
        R"({"current_weather":{"temperature":12,"weathercode":1.5}})",
        R"({"current_weather":{"temperature":12,"weathercode":100}})",
        R"({"current_weather":{"temperature":1e309,"weathercode":1}})",
        R"({"current_weather":[]})",
        "not json",
    };
    for (const std::string& body : invalid)
    {
        auto client = std::make_shared<FakeHttpClient>([body](const auto&, auto) {
            return success(body);
        });
        draxul::WeatherService weather(client);
        weather.start("51.5,-0.1");
        REQUIRE(wait_for_requests(*client, 1));
        weather.stop();
        CHECK_FALSE(weather.has_data());
    }
}

TEST_CASE("weather rejects invalid geocoding result shapes and coordinates", "[http][weather]")
{
    const std::vector<std::string> invalid = {
        R"({"results":[{"longitude":-1.0}]})",
        R"({"results":[{"latitude":"53","longitude":-1.0}]})",
        R"({"results":[{"latitude":91,"longitude":-1.0}]})",
        R"({"results":[{"latitude":53,"longitude":181}]})",
        R"({"results":{}})",
        R"({"results":[{"latitude":1e309,"longitude":0}]})",
    };
    for (const std::string& body : invalid)
    {
        auto client = std::make_shared<FakeHttpClient>([body](const auto&, auto) {
            return success(body);
        });
        draxul::WeatherService weather(client);
        weather.start("York");
        REQUIRE(wait_for_requests(*client, 1));
        weather.stop();
        CHECK_FALSE(weather.has_data());
        CHECK(client->requests().size() == 1);
    }
}

TEST_CASE("weather applies a one-megabyte response limit", "[http][weather]")
{
    std::atomic<std::size_t> observed_limit = 0;
    auto client = std::make_shared<FakeHttpClient>([&](const auto& request, auto) {
        draxul::http::Response response;
        observed_limit = request.max_response_bytes;
        response.status_code = 200;
        response.error = "HTTP response exceeded byte limit";
        return response;
    });
    draxul::WeatherService weather(client);
    weather.start("51.5,-0.1");
    REQUIRE(wait_for_requests(*client, 1));
    weather.stop();
    CHECK_FALSE(weather.has_data());
    CHECK(observed_limit == 1024 * 1024);
}

TEST_CASE("empty weather locations do not issue requests", "[http][weather]")
{
    auto client = std::make_shared<FakeHttpClient>([](const auto&, auto) {
        return success("{}");
    });
    draxul::WeatherService weather(client);
    weather.start("");
    weather.stop();
    CHECK(client->requests().empty());
}

TEST_CASE("weather stop cancels a blocked HTTP request", "[http][weather]")
{
    std::atomic<bool> entered = false;
    auto client = std::make_shared<FakeHttpClient>([&](const auto&, auto cancellation) {
        entered = true;
        while (!cancellation.is_cancelled())
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        draxul::http::Response response;
        response.cancelled = true;
        response.error = "request cancelled";
        return response;
    });
    draxul::WeatherService weather(client);
    weather.start("51.5,-0.1");
    for (int attempt = 0; attempt < 100 && !entered; ++attempt)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    REQUIRE(entered);

    const auto start = std::chrono::steady_clock::now();
    weather.stop();
    CHECK(std::chrono::steady_clock::now() - start < draxul::http::kCancellationShutdownBudget);
}
