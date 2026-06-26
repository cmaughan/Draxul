#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <draxul/satview/satview_catalog.h>

using draxul::satview::OrbitClass;
using draxul::satview::SatelliteObjectKind;
using draxul::satview::parse_celestrak_gp_json;

TEST_CASE("SatView catalog parses CelesTrak GP JSON records", "[satview][catalog]")
{
    const std::string json = R"json([
      {
        "OBJECT_NAME": "ISS (ZARYA)",
        "OBJECT_ID": "1998-067A",
        "OBJECT_TYPE": "PAYLOAD",
        "EPOCH": "2026-06-26T03:43:15.671136",
        "MEAN_MOTION": 15.49434804,
        "ECCENTRICITY": 0.00043588,
        "INCLINATION": 51.6325,
        "RA_OF_ASC_NODE": 255.7018,
        "ARG_OF_PERICENTER": 233.1468,
        "MEAN_ANOMALY": 126.9121,
        "EPHEMERIS_TYPE": 0,
        "CLASSIFICATION_TYPE": "U",
        "NORAD_CAT_ID": 25544,
        "ELEMENT_SET_NO": 999,
        "REV_AT_EPOCH": 57314,
        "BSTAR": 0.00017696555,
        "MEAN_MOTION_DOT": 9.461e-5,
        "MEAN_MOTION_DDOT": 0
      },
      {
        "OBJECT_NAME": "SAMPLE GEO R/B",
        "OBJECT_ID": "2026-001A",
        "EPOCH": "2026-06-26T00:00:00.000000",
        "MEAN_MOTION": 1.0027,
        "ECCENTRICITY": 0.0002,
        "INCLINATION": 0.1,
        "RA_OF_ASC_NODE": 5.0,
        "ARG_OF_PERICENTER": 220.0,
        "MEAN_ANOMALY": 140.0,
        "NORAD_CAT_ID": 900003
      }
    ])json";

    const auto result = parse_celestrak_gp_json(json, "stations", "https://celestrak.org/example");

    REQUIRE(result);
    REQUIRE(result.catalog.objects.size() == 2);
    CHECK(result.catalog.source_label == "stations");
    CHECK(result.catalog.source_url == "https://celestrak.org/example");

    const auto& iss = result.catalog.objects[0];
    CHECK(iss.object_name == "ISS (ZARYA)");
    CHECK(iss.object_id == "1998-067A");
    CHECK(iss.object_type == "PAYLOAD");
    CHECK(iss.object_kind == SatelliteObjectKind::Payload);
    CHECK(iss.norad_catalog_id == 25544);
    CHECK(iss.mean_motion_rev_per_day == Catch::Approx(15.49434804));
    CHECK(iss.bstar == Catch::Approx(0.00017696555));
    CHECK(iss.period_minutes == Catch::Approx(92.94).margin(0.1));
    CHECK(iss.orbit_class == OrbitClass::LowEarth);

    const auto& geo = result.catalog.objects[1];
    CHECK(geo.norad_catalog_id == 900003);
    CHECK(geo.orbit_class == OrbitClass::Geosynchronous);
    CHECK(geo.object_kind == SatelliteObjectKind::RocketBody);
    CHECK(geo.period_minutes == Catch::Approx(1436.13).margin(0.5));
}

TEST_CASE("SatView catalog skips malformed GP objects", "[satview][catalog]")
{
    const std::string json = R"json([
      {
        "OBJECT_NAME": "MISSING ELEMENTS",
        "NORAD_CAT_ID": 1
      },
      {
        "OBJECT_NAME": "VALID MEO",
        "OBJECT_ID": "2026-002A",
        "EPOCH": "2026-06-26T00:00:00.000000",
        "MEAN_MOTION": "2.0056",
        "ECCENTRICITY": 0.01,
        "INCLINATION": 55.0,
        "RA_OF_ASC_NODE": 45.0,
        "ARG_OF_PERICENTER": 12.0,
        "MEAN_ANOMALY": 348.0,
        "NORAD_CAT_ID": "900002"
      }
    ])json";

    const auto result = parse_celestrak_gp_json(json);

    REQUIRE(result);
    REQUIRE(result.catalog.objects.size() == 1);
    CHECK(result.catalog.skipped_records == 1);
    CHECK(result.catalog.objects[0].norad_catalog_id == 900002);
    CHECK(result.catalog.objects[0].orbit_class == OrbitClass::MediumEarth);
}

TEST_CASE("SatView sample catalog fixture loads offline", "[satview][catalog]")
{
    const auto result = draxul::satview::load_sample_satellite_catalog();

    REQUIRE(result);
    REQUIRE(result.catalog.objects.size() == 4);
    CHECK(result.catalog.objects[0].orbit_class == OrbitClass::LowEarth);
    CHECK(result.catalog.objects[0].object_kind == SatelliteObjectKind::Payload);
    CHECK(result.catalog.objects[1].orbit_class == OrbitClass::MediumEarth);
    CHECK(result.catalog.objects[1].object_kind == SatelliteObjectKind::RocketBody);
    CHECK(result.catalog.objects[2].orbit_class == OrbitClass::Geosynchronous);
    CHECK(result.catalog.objects[2].object_kind == SatelliteObjectKind::Debris);
    CHECK(result.catalog.objects[3].orbit_class == OrbitClass::HighlyElliptical);
    CHECK(result.catalog.objects[3].object_kind == SatelliteObjectKind::Payload);
}
