#pragma once

#include <draxul/http/http_client.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace draxul
{

// Periodically fetches the current temperature via the Open-Meteo API.
// Uses the platform-native HTTP stack. Degrades silently if the network is
// down or the location string cannot be geocoded.
//
// Usage:
//   weather.start("York, UK");   // or "51.5,-0.1"
//   ...
//   auto text = weather.display_text();  // "\U0001F321\uFE0F 18°C" or ""
class WeatherService
{
public:
    WeatherService();
    explicit WeatherService(std::shared_ptr<http::IHttpClient> http_client);
    ~WeatherService();

    WeatherService(const WeatherService&) = delete;
    WeatherService& operator=(const WeatherService&) = delete;

    // Start periodic fetching for the given location. If location looks like
    // "lat,lon" (two numbers), uses it directly. Otherwise geocodes via
    // Open-Meteo. No-op if location is empty or the worker is already active.
    void start(const std::string& location);

    // Cancel any in-flight request, join the worker, and clear published data.
    void stop();

    // Dependency injection for App-level reload tests. Replacing the client
    // stops any existing worker before swapping transports.
    void set_http_client(std::shared_ptr<http::IHttpClient> http_client);

    // Thread-safe. Returns "" if no data is available yet.
    [[nodiscard]] std::string display_text() const;

    // Thread-safe accessors for separate emoji and temperature parts.
    [[nodiscard]] std::string emoji() const;
    [[nodiscard]] std::string temperature() const;

    // Returns true if a fetch has completed at least once.
    [[nodiscard]] bool has_data() const noexcept
    {
        return has_data_.load(std::memory_order_relaxed);
    }

private:
    void worker_func(std::string location);
    bool try_geocode(const std::string& query, double& latitude, double& longitude);
    bool fetch_temperature(double latitude, double longitude,
        double& temperature_c, int& weather_code);
    std::string get(std::string url);

    mutable std::mutex mutex_;
    std::string emoji_;
    std::string temperature_;
    std::atomic<bool> has_data_{ false };
    std::atomic<bool> running_{ false };
    std::thread thread_;
    std::shared_ptr<http::IHttpClient> http_client_;
    http::CancellationSource cancellation_;
};

} // namespace draxul
