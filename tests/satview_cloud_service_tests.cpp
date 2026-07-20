#include "temp_dir.h"
#include <draxul/satview/satview_cloud_service.h>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <string>
#include <thread>
#include <utility>

using draxul::satview::SatViewCloudService;

namespace
{

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

bool wait_for_idle(SatViewCloudService& service)
{
    for (int i = 0; i < 200; ++i)
    {
        service.pump();
        const auto state = service.status().refresh_state;
        if (state != SatViewCloudService::RefreshState::Loading
            && state != SatViewCloudService::RefreshState::Refreshing)
        {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

SatViewCloudService::Config config_for(
    const std::filesystem::path& cache_directory,
    SatViewCloudService::FetchFunction fetch)
{
    SatViewCloudService::Config config;
    config.cache_directory = cache_directory;
    config.refresh_interval = std::chrono::hours(3);
    config.fetch = std::move(fetch);
    return config;
}

} // namespace

TEST_CASE("SatView cloud service fetches, decodes, and caches an image", "[satview][cloud][service]")
{
    draxul::tests::TempDir temp("satview-cloud-service");
    int fetch_calls = 0;
    SatViewCloudService service;
    service.start(config_for(temp.path, [&](std::string_view url, std::string& error) {
        ++fetch_calls;
        CHECK(url == SatViewCloudService::default_source_url());
        error.clear();
        return tiny_cloud_ppm();
    }));

    REQUIRE(wait_for_idle(service));
    const auto image = service.take_pending_image();
    REQUIRE(image);
    CHECK(image->width == 2);
    CHECK(image->height == 1);
    CHECK(image->rgba.size() == 8);
    CHECK(fetch_calls == 1);
    CHECK(service.status().data_source == SatViewCloudService::DataSource::Live);
    CHECK(std::filesystem::exists(SatViewCloudService::cache_image_path(temp.path)));
}

TEST_CASE("SatView cloud service reuses a fresh cached image", "[satview][cloud][service]")
{
    draxul::tests::TempDir temp("satview-cloud-service");
    {
        SatViewCloudService seeder;
        seeder.start(config_for(temp.path, [](std::string_view, std::string& error) {
            error.clear();
            return tiny_cloud_ppm();
        }));
        REQUIRE(wait_for_idle(seeder));
    }

    int fetch_calls = 0;
    SatViewCloudService service;
    service.start(config_for(temp.path, [&](std::string_view, std::string& error) {
        ++fetch_calls;
        error = "fresh cache should avoid a download";
        return std::string{};
    }));

    REQUIRE(wait_for_idle(service));
    const auto image = service.take_pending_image();
    REQUIRE(image);
    CHECK(image->width == 2);
    CHECK(image->height == 1);
    CHECK(fetch_calls == 0);
    CHECK(service.status().data_source == SatViewCloudService::DataSource::Cache);
}

TEST_CASE("SatView cloud service backs off after stale-cache refresh failure", "[satview][cloud][service]")
{
    draxul::tests::TempDir temp("satview-cloud-service");
    {
        SatViewCloudService seeder;
        seeder.start(config_for(temp.path, [](std::string_view, std::string& error) {
            error.clear();
            return tiny_cloud_ppm();
        }));
        REQUIRE(wait_for_idle(seeder));
    }

    const auto cache_path = SatViewCloudService::cache_image_path(temp.path);
    std::filesystem::last_write_time(
        cache_path, std::filesystem::file_time_type::clock::now() - std::chrono::hours(4));

    int fetch_calls = 0;
    SatViewCloudService service;
    service.start(config_for(temp.path, [&](std::string_view, std::string& error) {
        ++fetch_calls;
        error = "network unavailable";
        return std::string{};
    }));

    REQUIRE(wait_for_idle(service));
    CHECK(service.take_pending_image());
    CHECK(service.status().data_source == SatViewCloudService::DataSource::Cache);
    CHECK(service.status().refresh_state == SatViewCloudService::RefreshState::Failed);
    for (int i = 0; i < 5; ++i)
        service.pump();
    CHECK(fetch_calls == 1);
}
