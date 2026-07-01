#include "temp_dir.h"

#include <catch2/catch_test_macros.hpp>
#include <draxul/satview/satview_catalog_service.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <thread>
#include <utility>

using draxul::satview::SatViewCatalogService;

namespace
{

const char* kOneObjectJson = R"json([
  {
    "OBJECT_NAME": "LIVE ONE",
    "OBJECT_ID": "2026-003A",
    "EPOCH": "2026-06-26T00:00:00.000000",
    "MEAN_MOTION": 15.5,
    "ECCENTRICITY": 0.0005,
    "INCLINATION": 51.6,
    "RA_OF_ASC_NODE": 120.0,
    "ARG_OF_PERICENTER": 87.0,
    "MEAN_ANOMALY": 273.0,
    "NORAD_CAT_ID": 910001
  }
])json";

const char* kTwoObjectJson = R"json([
  {
    "OBJECT_NAME": "LIVE TWO A",
    "OBJECT_ID": "2026-004A",
    "EPOCH": "2026-06-26T00:00:00.000000",
    "MEAN_MOTION": 15.5,
    "ECCENTRICITY": 0.0005,
    "INCLINATION": 51.6,
    "RA_OF_ASC_NODE": 120.0,
    "ARG_OF_PERICENTER": 87.0,
    "MEAN_ANOMALY": 273.0,
    "NORAD_CAT_ID": 920001
  },
  {
    "OBJECT_NAME": "LIVE TWO B",
    "OBJECT_ID": "2026-004B",
    "EPOCH": "2026-06-26T00:05:00.000000",
    "MEAN_MOTION": 1.0027,
    "ECCENTRICITY": 0.0002,
    "INCLINATION": 0.1,
    "RA_OF_ASC_NODE": 5.0,
    "ARG_OF_PERICENTER": 220.0,
    "MEAN_ANOMALY": 140.0,
    "NORAD_CAT_ID": 920002
  }
])json";

const char* kOneObjectSatcat =
    "OBJECT_NAME,NORAD_CAT_ID,OBJECT_ID,OBJECT_TYPE,OPS_STATUS_CODE,OWNER,DECAY_DATE,PERIOD,"
    "INCLINATION,APOGEE,PERIGEE,RCS,DATA_STATUS_CODE,ORBIT_CENTER,ORBIT_TYPE\n"
    "LIVE ONE,910001,2026-003A,PAY,+,US,,92.9,51.6,430,410,100,,EA,ORB\n";

const char* kTwoObjectSatcat =
    "OBJECT_NAME,NORAD_CAT_ID,OBJECT_ID,OBJECT_TYPE,OPS_STATUS_CODE,OWNER,DECAY_DATE,PERIOD,"
    "INCLINATION,APOGEE,PERIGEE,RCS,DATA_STATUS_CODE,ORBIT_CENTER,ORBIT_TYPE\n"
    "LIVE TWO A,920001,2026-004A,PAY,+,US,,92.9,51.6,430,410,100,,EA,ORB\n"
    "LIVE TWO B,920002,2026-004B,PAY,+,US,,1436,0.1,35790,35770,120,,EA,ORB\n";

bool wait_for_idle(SatViewCatalogService& service)
{
    for (int i = 0; i < 200; ++i)
    {
        service.pump();
        if (!service.refresh_in_flight())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

SatViewCatalogService::Config config_for(
    const std::filesystem::path& cache_dir,
    SatViewCatalogService::FetchFunction fetch)
{
    SatViewCatalogService::Config config;
    config.cache_directory = cache_dir;
    config.refresh_interval = std::chrono::hours(2);
    config.fetch = std::move(fetch);
    return config;
}

SatViewCatalogService::FetchFunction payload_fetch(
    std::string gp,
    std::string satcat,
    int* calls = nullptr)
{
    return [gp = std::move(gp), satcat = std::move(satcat), calls](
               std::string_view url,
               std::string& error) {
        if (calls)
            ++*calls;
        error.clear();
        if (url.find("satcat.csv") != std::string_view::npos)
            return satcat;
        return gp;
    };
}

} // namespace

TEST_CASE("SatView catalog service builds encoded CelesTrak group URLs", "[satview][catalog][service]")
{
    const auto url = SatViewCatalogService::default_celestrak_url("Visual + Active");
    CHECK(url == "https://celestrak.org/NORAD/elements/gp.php?GROUP=visual%20%2B%20active&FORMAT=json");
    CHECK(SatViewCatalogService::default_satcat_url() == "https://celestrak.org/pub/satcat.csv");
}

TEST_CASE("SatView catalog service fetches live catalog and writes cache", "[satview][catalog][service]")
{
    draxul::tests::TempDir temp("satview-catalog-service");
    int fetch_calls = 0;
    SatViewCatalogService service;

    service.start(config_for(temp.path, [&](std::string_view url, std::string& error) {
        ++fetch_calls;
        error.clear();
        if (url.find("satcat.csv") != std::string_view::npos)
            return std::string(kOneObjectSatcat);
        CHECK(url.find("GROUP=active") != std::string_view::npos);
        return std::string(kOneObjectJson);
    }));

    REQUIRE(wait_for_idle(service));
    const auto status = service.status();
    CHECK(fetch_calls == 2);
    CHECK(status.data_source == SatViewCatalogService::DataSource::Live);
    CHECK(status.refresh_state == SatViewCatalogService::RefreshState::Idle);
    CHECK(status.gp.data_source == SatViewCatalogService::DataSource::Live);
    CHECK(status.satcat.data_source == SatViewCatalogService::DataSource::Live);
    CHECK(status.object_count == 1);
    CHECK(status.renderable_count == 1);
    CHECK(status.text.find("GP: live 1") != std::string::npos);
    CHECK(status.text.find("SATCAT: live 1") != std::string::npos);
    CHECK(std::filesystem::exists(temp.path / "celestrak_active_gp.json"));
    CHECK(std::filesystem::exists(temp.path / "celestrak_active_gp.meta"));
    CHECK(std::filesystem::exists(temp.path / "celestrak_satcat.csv"));
    CHECK(std::filesystem::exists(temp.path / "celestrak_satcat.meta"));
}

TEST_CASE("SatView catalog service uses fresh cache without refetching", "[satview][catalog][service]")
{
    draxul::tests::TempDir temp("satview-catalog-service");
    {
        SatViewCatalogService seeder;
        seeder.start(config_for(temp.path, payload_fetch(kOneObjectJson, kOneObjectSatcat)));
        REQUIRE(wait_for_idle(seeder));
    }

    int fetch_calls = 0;
    SatViewCatalogService service;
    service.start(config_for(temp.path, [&](std::string_view, std::string& error) {
        ++fetch_calls;
        error = "should not fetch";
        return std::string{};
    }));

    service.pump();
    const auto status = service.status();
    CHECK(fetch_calls == 0);
    CHECK(status.data_source == SatViewCatalogService::DataSource::Cache);
    CHECK(status.refresh_state == SatViewCatalogService::RefreshState::Idle);
    CHECK(status.gp.data_source == SatViewCatalogService::DataSource::Cache);
    CHECK(status.satcat.data_source == SatViewCatalogService::DataSource::Cache);
    CHECK(status.object_count == 1);
    CHECK(status.text.find("GP: cached 1") != std::string::npos);
    CHECK(status.text.find("SATCAT: cached 1") != std::string::npos);
}

TEST_CASE("SatView catalog service skips manual refresh while cache is fresh", "[satview][catalog][service]")
{
    draxul::tests::TempDir temp("satview-catalog-service");
    {
        SatViewCatalogService seeder;
        seeder.start(config_for(temp.path, payload_fetch(kOneObjectJson, kOneObjectSatcat)));
        REQUIRE(wait_for_idle(seeder));
    }

    int fetch_calls = 0;
    SatViewCatalogService service;
    service.start(config_for(temp.path, [&](std::string_view, std::string& error) {
        ++fetch_calls;
        error = "should not fetch";
        return std::string{};
    }));

    CHECK_FALSE(service.request_refresh());
    service.pump();
    const auto status = service.status();
    CHECK(fetch_calls == 0);
    CHECK(status.data_source == SatViewCatalogService::DataSource::Cache);
    CHECK(status.refresh_state == SatViewCatalogService::RefreshState::Idle);
    CHECK(status.object_count == 1);
    CHECK(status.text.find("GP: cached 1") != std::string::npos);
}

TEST_CASE("SatView catalog service refreshes stale cache", "[satview][catalog][service]")
{
    draxul::tests::TempDir temp("satview-catalog-service");
    {
        SatViewCatalogService seeder;
        seeder.start(config_for(temp.path, payload_fetch(kOneObjectJson, kOneObjectSatcat)));
        REQUIRE(wait_for_idle(seeder));
    }

    int fetch_calls = 0;
    auto config = config_for(temp.path, payload_fetch(kTwoObjectJson, kTwoObjectSatcat, &fetch_calls));
    config.refresh_interval = std::chrono::seconds::zero();
    config.satcat_refresh_interval = std::chrono::seconds::zero();

    SatViewCatalogService service;
    service.start(std::move(config));
    REQUIRE(wait_for_idle(service));

    const auto status = service.status();
    CHECK(fetch_calls == 2);
    CHECK(status.data_source == SatViewCatalogService::DataSource::Live);
    CHECK(status.object_count == 2);
    CHECK(status.text.find("merged 2") != std::string::npos);
}

TEST_CASE("SatView catalog service keeps stale cache when refresh fails", "[satview][catalog][service]")
{
    draxul::tests::TempDir temp("satview-catalog-service");
    {
        SatViewCatalogService seeder;
        seeder.start(config_for(temp.path, payload_fetch(kOneObjectJson, kOneObjectSatcat)));
        REQUIRE(wait_for_idle(seeder));
    }

    auto config = config_for(temp.path, [](std::string_view, std::string& error) {
        error = "network unavailable";
        return std::string{};
    });
    config.refresh_interval = std::chrono::seconds::zero();
    config.satcat_refresh_interval = std::chrono::seconds::zero();

    SatViewCatalogService service;
    service.start(std::move(config));
    REQUIRE(wait_for_idle(service));

    const auto status = service.status();
    CHECK(status.data_source == SatViewCatalogService::DataSource::Cache);
    CHECK(status.refresh_state == SatViewCatalogService::RefreshState::Failed);
    CHECK(status.object_count == 1);
    CHECK(status.error.find("GP: network unavailable") != std::string::npos);
    CHECK(status.error.find("SATCAT: network unavailable") != std::string::npos);
    CHECK(status.text.find("GP: cached 1") != std::string::npos);
    CHECK(status.text.find("SATCAT: cached 1") != std::string::npos);
}

TEST_CASE("SatView catalog service refreshes GP and SATCAT on independent cadences", "[satview][catalog][service]")
{
    draxul::tests::TempDir temp("satview-catalog-service");
    {
        SatViewCatalogService seeder;
        seeder.start(config_for(temp.path, payload_fetch(kOneObjectJson, kOneObjectSatcat)));
        REQUIRE(wait_for_idle(seeder));
    }

    int fetch_calls = 0;
    auto config = config_for(temp.path, [&](std::string_view url, std::string& error) {
        ++fetch_calls;
        CHECK(url.find("GROUP=active") != std::string_view::npos);
        error.clear();
        return std::string(kTwoObjectJson);
    });
    config.refresh_interval = std::chrono::seconds::zero();
    config.satcat_refresh_interval = std::chrono::hours(12);

    SatViewCatalogService service;
    service.start(std::move(config));
    REQUIRE(wait_for_idle(service));

    const auto status = service.status();
    CHECK(fetch_calls == 1);
    CHECK(status.gp.data_source == SatViewCatalogService::DataSource::Live);
    CHECK(status.satcat.data_source == SatViewCatalogService::DataSource::Cache);
    CHECK(status.object_count == 3);
}

TEST_CASE("SatView catalog service keeps one cached source when the other refresh fails", "[satview][catalog][service]")
{
    draxul::tests::TempDir temp("satview-catalog-service");
    {
        SatViewCatalogService seeder;
        seeder.start(config_for(temp.path, payload_fetch(kOneObjectJson, kOneObjectSatcat)));
        REQUIRE(wait_for_idle(seeder));
    }

    auto config = config_for(temp.path, [&](std::string_view url, std::string& error) {
        if (url.find("satcat.csv") != std::string_view::npos)
        {
            error = "SATCAT unavailable";
            return std::string{};
        }
        error.clear();
        return std::string(kTwoObjectJson);
    });
    config.refresh_interval = std::chrono::seconds::zero();
    config.satcat_refresh_interval = std::chrono::seconds::zero();

    SatViewCatalogService service;
    service.start(std::move(config));
    REQUIRE(wait_for_idle(service));

    const auto status = service.status();
    CHECK(status.refresh_state == SatViewCatalogService::RefreshState::Failed);
    CHECK(status.gp.data_source == SatViewCatalogService::DataSource::Live);
    CHECK(status.satcat.data_source == SatViewCatalogService::DataSource::Cache);
    CHECK(status.satcat.error == "SATCAT unavailable");
    CHECK(status.object_count == 3);
}
