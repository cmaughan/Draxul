#include "temp_dir.h"
#include <draxul/satview/satview_cloud_service.h>

#include <catch2/catch_test_macros.hpp>
#include <draxul/http/http_client.h>
#include <draxul/satview/satview_catalog_service.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace
{

class BlockingHttpClient final : public draxul::http::IHttpClient
{
public:
    draxul::http::Response get(
        const draxul::http::Request&,
        draxul::http::CancellationToken cancellation) override
    {
        ++calls;
        entered = true;
        while (!cancellation.is_cancelled())
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        ++cancellations;
        draxul::http::Response response;
        response.cancelled = true;
        response.error = "request cancelled";
        return response;
    }

    std::atomic<bool> entered = false;
    std::atomic<int> calls = 0;
    std::atomic<int> cancellations = 0;
};

constexpr std::string_view kCatalogJson = R"json([
  {
    "OBJECT_NAME": "LAST GOOD",
    "OBJECT_ID": "2026-099A",
    "EPOCH": "2026-07-01T00:00:00.000000",
    "MEAN_MOTION": 15.5,
    "ECCENTRICITY": 0.0005,
    "INCLINATION": 51.6,
    "RA_OF_ASC_NODE": 120.0,
    "ARG_OF_PERICENTER": 87.0,
    "MEAN_ANOMALY": 273.0,
    "NORAD_CAT_ID": 990001
  }
])json";

constexpr std::string_view kSatcatCsv = "OBJECT_NAME,NORAD_CAT_ID,OBJECT_ID,OBJECT_TYPE,OPS_STATUS_CODE,OWNER,DECAY_DATE,PERIOD,"
                                        "INCLINATION,APOGEE,PERIGEE,RCS,DATA_STATUS_CODE,ORBIT_CENTER,ORBIT_TYPE\n"
                                        "LAST GOOD,990001,2026-099A,PAY,+,US,,92.9,51.6,430,410,100,,EA,ORB\n";

std::string tiny_cloud_ppm()
{
    std::string image = "P6\n2 1\n255\n";
    const char pixels[] = {
        static_cast<char>(255),
        0,
        0,
        0,
        static_cast<char>(255),
        0,
    };
    image.append(pixels, sizeof(pixels));
    return image;
}

void wait_until_entered(const BlockingHttpClient& client)
{
    for (int attempt = 0; attempt < 100 && !client.entered; ++attempt)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    REQUIRE(client.entered);
}

bool wait_for_catalog_idle(draxul::satview::SatViewCatalogService& service)
{
    for (int attempt = 0; attempt < 200; ++attempt)
    {
        service.pump();
        if (!service.refresh_in_flight())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

bool wait_for_cloud_idle(draxul::satview::SatViewCloudService& service)
{
    for (int attempt = 0; attempt < 200; ++attempt)
    {
        service.pump();
        const auto state = service.status().refresh_state;
        if (state != draxul::satview::SatViewCloudService::RefreshState::Loading
            && state != draxul::satview::SatViewCloudService::RefreshState::Refreshing)
        {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

draxul::satview::SatViewCatalogService::Config catalog_seed_config(
    const std::filesystem::path& cache_directory)
{
    draxul::satview::SatViewCatalogService::Config config;
    config.cache_directory = cache_directory;
    config.refresh_interval = std::chrono::hours(2);
    config.satcat_refresh_interval = std::chrono::hours(12);
    config.fetch = [](std::string_view url, std::string& error) {
        error.clear();
        return std::string(
            url.find("satcat.csv") != std::string_view::npos ? kSatcatCsv : kCatalogJson);
    };
    return config;
}

draxul::satview::SatViewCloudService::Config cloud_seed_config(
    const std::filesystem::path& cache_directory)
{
    draxul::satview::SatViewCloudService::Config config;
    config.cache_directory = cache_directory;
    config.refresh_interval = std::chrono::hours(3);
    config.fetch = [](std::string_view, std::string& error) {
        error.clear();
        return tiny_cloud_ppm();
    };
    return config;
}

} // namespace

TEST_CASE("SatView catalog stop cancels a blocked native fetch", "[satview][http][service]")
{
    draxul::tests::TempDir temp("satview-http-catalog-stop");
    auto client = std::make_shared<BlockingHttpClient>();
    draxul::satview::SatViewCatalogService::Config config;
    config.cache_directory = temp.path;
    config.http_client = client;
    draxul::satview::SatViewCatalogService service;
    service.start(std::move(config));
    wait_until_entered(*client);

    const auto start = std::chrono::steady_clock::now();
    service.stop();
    CHECK(std::chrono::steady_clock::now() - start < draxul::http::kCancellationShutdownBudget);
    CHECK(client->cancellations > 0);
}

TEST_CASE("SatView cloud stop cancels a blocked native fetch", "[satview][http][service]")
{
    draxul::tests::TempDir temp("satview-http-cloud-stop");
    auto client = std::make_shared<BlockingHttpClient>();
    draxul::satview::SatViewCloudService::Config config;
    config.cache_directory = temp.path;
    config.http_client = client;
    draxul::satview::SatViewCloudService service;
    service.start(std::move(config));
    wait_until_entered(*client);

    const auto start = std::chrono::steady_clock::now();
    service.stop();
    CHECK(std::chrono::steady_clock::now() - start < draxul::http::kCancellationShutdownBudget);
    CHECK(client->cancellations == 1);
}

TEST_CASE("SatView catalog cancellation preserves the loaded last-good catalog and cache", "[satview][http][service][cache]")
{
    draxul::tests::TempDir temp("satview-http-catalog-last-good");
    {
        draxul::satview::SatViewCatalogService seeder;
        seeder.start(catalog_seed_config(temp.path));
        REQUIRE(wait_for_catalog_idle(seeder));
        REQUIRE(seeder.catalog().objects.size() == 1);
    }

    auto client = std::make_shared<BlockingHttpClient>();
    auto config = catalog_seed_config(temp.path);
    config.fetch = {};
    config.http_client = client;
    config.refresh_interval = std::chrono::seconds::zero();
    config.satcat_refresh_interval = std::chrono::seconds::zero();

    draxul::satview::SatViewCatalogService service;
    service.start(std::move(config));
    wait_until_entered(*client);

    const auto before_cancel = service.catalog();
    REQUIRE(before_cancel.objects.size() == 1);
    CHECK(before_cancel.objects.front().object_name == "LAST GOOD");
    CHECK(service.status().data_source == draxul::satview::SatViewCatalogService::DataSource::Cache);
    const auto generation = service.catalog_generation();

    service.stop();
    CHECK(client->cancellations > 0);
    const auto after_cancel = service.catalog();
    REQUIRE(after_cancel.objects.size() == 1);
    CHECK(after_cancel.objects.front().object_name == "LAST GOOD");
    CHECK(service.catalog_generation() == generation);

    int unexpected_fetches = 0;
    auto verify_config = catalog_seed_config(temp.path);
    verify_config.fetch = [&](std::string_view, std::string& error) {
        ++unexpected_fetches;
        error = "the preserved cache should be fresh";
        return std::string{};
    };
    draxul::satview::SatViewCatalogService verifier;
    verifier.start(std::move(verify_config));
    verifier.pump();
    const auto verified = verifier.catalog();
    REQUIRE(verified.objects.size() == 1);
    CHECK(verified.objects.front().object_name == "LAST GOOD");
    CHECK(verifier.status().data_source == draxul::satview::SatViewCatalogService::DataSource::Cache);
    CHECK(unexpected_fetches == 0);
}

TEST_CASE("SatView cloud cancellation preserves the loaded last-good image and cache", "[satview][http][service][cache]")
{
    draxul::tests::TempDir temp("satview-http-cloud-last-good");
    {
        draxul::satview::SatViewCloudService seeder;
        seeder.start(cloud_seed_config(temp.path));
        REQUIRE(wait_for_cloud_idle(seeder));
        REQUIRE(seeder.take_pending_image());
    }

    auto client = std::make_shared<BlockingHttpClient>();
    auto config = cloud_seed_config(temp.path);
    config.fetch = {};
    config.http_client = client;

    draxul::satview::SatViewCloudService service;
    service.start(std::move(config));
    REQUIRE(wait_for_cloud_idle(service));
    CHECK(service.status().data_source == draxul::satview::SatViewCloudService::DataSource::Cache);
    REQUIRE(service.request_refresh());
    wait_until_entered(*client);
    CHECK(service.status().data_source == draxul::satview::SatViewCloudService::DataSource::Cache);

    service.stop();
    CHECK(client->cancellations == 1);
    const auto last_good_image = service.take_pending_image();
    REQUIRE(last_good_image);
    CHECK(last_good_image->width == 2);
    CHECK(last_good_image->height == 1);
    CHECK(last_good_image->rgba.size() == 8);

    int unexpected_fetches = 0;
    auto verify_config = cloud_seed_config(temp.path);
    verify_config.fetch = [&](std::string_view, std::string& error) {
        ++unexpected_fetches;
        error = "the preserved cache should be fresh";
        return std::string{};
    };
    draxul::satview::SatViewCloudService verifier;
    verifier.start(std::move(verify_config));
    REQUIRE(wait_for_cloud_idle(verifier));
    const auto verified_image = verifier.take_pending_image();
    REQUIRE(verified_image);
    CHECK(verified_image->width == 2);
    CHECK(verified_image->height == 1);
    CHECK(verifier.status().data_source == draxul::satview::SatViewCloudService::DataSource::Cache);
    CHECK(unexpected_fetches == 0);
}
